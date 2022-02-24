/*
 * Copyright (C) 2020 Andrew Ayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <algorithm>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>

namespace {
	struct Addresses {
		struct sockaddr_storage	local_addr;
		socklen_t		local_addrlen{};
		struct sockaddr_storage	remote_addr;
		socklen_t		remote_addrlen{};
	};

	const unsigned char protocol_signature[12] = { 0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A };

	enum {
		command_local = 0x00,
		command_proxy = 0x01
	};

	enum {
		family_unspecified = 0x00,
		family_tcp4        = 0x11,
		family_udp4        = 0x12,
		family_tcp6        = 0x21,
		family_udp6        = 0x22
	};

	std::mutex			mutex;
	std::map<int, sa_family_t>	listeners;
	std::map<int, Addresses>	fake_addresses;
}

static bool is_stream_socket (int sockfd)
{
	int		type = 0;
	socklen_t	optlen = sizeof(type);

	return getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &type, &optlen) == 0 && type == SOCK_STREAM;
}

static int read_full (int fd, unsigned char* p, std::size_t len)
{
	while (len > 0) {
		const ssize_t bytes_read{read(fd, p, len)};
		if (bytes_read < 0) {
			return -1;
		} else if (bytes_read == 0) {
			errno = EPROTO;
			return -1;
		}
		p += bytes_read;
		len -= bytes_read;
	}
	return 0;
}

static std::uint16_t decode_be16 (unsigned char* p)
{
	return (static_cast<std::uint16_t>(p[0]) << 8) | (static_cast<std::uint16_t>(p[1]) << 0);
}

static int read_proxy_header (Addresses* client_addresses, int client_fd, sa_family_t local_family)
try {
	unsigned char		preamble[16];
	if (read_full(client_fd, preamble, sizeof(preamble)) == -1) {
		return -1;
	}

	if (std::memcmp(preamble, protocol_signature, 12) != 0) {
		errno = EPROTO;
		return -1;
	}
	const std::uint8_t	command{preamble[12]};
	const std::uint8_t	proxied_family{preamble[13]};
	const std::uint16_t	payload_length{decode_be16(preamble + 14)};

	std::unique_ptr<unsigned char[]> payload_buffer(new unsigned char[payload_length]);
	unsigned char*		payload = payload_buffer.get();
	if (read_full(client_fd, payload, payload_length) == -1) {
		return -1;
	}

	if (command != command_proxy) {
		errno = EPROTO;
		return -1;
	}

	if (local_family == AF_INET) {
		struct sockaddr_in	remote_addr;
		struct sockaddr_in	local_addr;

		std::memset(&remote_addr, 0, sizeof(remote_addr));
		std::memset(&local_addr, 0, sizeof(local_addr));

		remote_addr.sin_family = AF_INET;
		local_addr.sin_family = AF_INET;

		if (proxied_family == family_tcp4 || proxied_family == family_udp4) {
			if (payload_length < 12) {
				errno = EPROTO;
				return -1;
			}
			std::memcpy(&remote_addr.sin_addr, payload + 0, 4);
			std::memcpy(&local_addr.sin_addr, payload + 4, 4);
			std::memcpy(&remote_addr.sin_port, payload + 8, 2);
			std::memcpy(&local_addr.sin_port, payload + 10, 2);
		} else if (proxied_family == family_tcp6 || proxied_family == family_udp6) {
			if (payload_length < 36) {
				errno = EPROTO;
				return -1;
			}
			// Unable to represent IPv6 address in a sockaddr_in so just leave IP address unspecified
			std::memcpy(&remote_addr.sin_port, payload + 32, 2);
			std::memcpy(&local_addr.sin_port, payload + 34, 2);
		}

		std::memcpy(&client_addresses->remote_addr, &remote_addr, sizeof(remote_addr));
		std::memcpy(&client_addresses->local_addr, &local_addr, sizeof(local_addr));

		client_addresses->remote_addrlen = sizeof(remote_addr);
		client_addresses->local_addrlen = sizeof(local_addr);

	} else if (local_family == AF_INET6) {
		struct sockaddr_in6	remote_addr;
		struct sockaddr_in6	local_addr;

		std::memset(&remote_addr, 0, sizeof(remote_addr));
		std::memset(&local_addr, 0, sizeof(local_addr));

		remote_addr.sin6_family = AF_INET6;
		local_addr.sin6_family = AF_INET6;

		if (proxied_family == family_tcp4 || proxied_family == family_udp4) {
			if (payload_length < 12) {
				errno = EPROTO;
				return -1;
			}
			// Represent IPv4 address in sockaddr_in6 as a mapped address
			std::memset(remote_addr.sin6_addr.s6_addr + 10, 0xff, 2);
			std::memset(local_addr.sin6_addr.s6_addr + 10, 0xff, 2);

			std::memcpy(remote_addr.sin6_addr.s6_addr + 12, payload + 0, 4);
			std::memcpy(local_addr.sin6_addr.s6_addr + 12, payload + 4, 4);
			std::memcpy(&remote_addr.sin6_port, payload + 8, 2);
			std::memcpy(&local_addr.sin6_port, payload + 10, 2);
		} else if (proxied_family == family_tcp6 || proxied_family == family_udp6) {
			if (payload_length < 36) {
				errno = EPROTO;
				return -1;
			}
			std::memcpy(&remote_addr.sin6_addr.s6_addr, payload + 0, 16);
			std::memcpy(&local_addr.sin6_addr.s6_addr, payload + 16, 16);
			std::memcpy(&remote_addr.sin6_port, payload + 32, 2);
			std::memcpy(&local_addr.sin6_port, payload + 34, 2);
		}

		std::memcpy(&client_addresses->remote_addr, &remote_addr, sizeof(remote_addr));
		std::memcpy(&client_addresses->local_addr, &local_addr, sizeof(local_addr));

		client_addresses->remote_addrlen = sizeof(remote_addr);
		client_addresses->local_addrlen = sizeof(local_addr);

	} else {
		errno = EINVAL;
		return -1;
	}

	return 0;
} catch (const std::bad_alloc&) {
	errno = ENOMEM;
	return -1;
}

static int copy_sockaddr (struct sockaddr* dest, socklen_t* destlen, const struct sockaddr_storage& src, socklen_t srclen)
{
	if (*destlen < 0) {
		errno = EINVAL;
		return -1;
	}

	std::memcpy(dest, &src, std::min(*destlen, srclen));
	*destlen = srclen;
	return 0;
}

static void register_listener (int sockfd, sa_family_t listener_family, const Addresses& listener_addresses)
{
	std::lock_guard<std::mutex>	lock(mutex);
	listeners[sockfd] = listener_family;
	fake_addresses[sockfd] = listener_addresses;
}

static void register_client (int sockfd, const Addresses& client_addresses)
{
	std::lock_guard<std::mutex>	lock(mutex);
	fake_addresses[sockfd] = client_addresses;
}

static bool find_listener (int sockfd, sa_family_t* family)
{
	std::lock_guard<std::mutex>	lock(mutex);
	auto				it(listeners.find(sockfd));
	if (it == listeners.end()) {
		return false;
	}
	*family = it->second;
	return true;
}

static void forget_socket (int sockfd)
{
	std::lock_guard<std::mutex>	lock(mutex);
	listeners.erase(sockfd);
	fake_addresses.erase(sockfd);
}


extern "C" int bind (int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
	const auto	real_bind = reinterpret_cast<decltype(bind)*>(dlsym(RTLD_NEXT, "bind"));

	sa_family_t	addr_family;
	if (addr == nullptr || addrlen < sizeof(addr_family)) {
		errno = EINVAL;
		return -1;
	}
	std::memcpy(&addr_family, addr, sizeof(addr_family));
	const bool	is_stream_socket{::is_stream_socket(sockfd)};
	int		portno{};
	if (addr_family == AF_INET && is_stream_socket) {
		struct sockaddr_in	sin;
		if (addrlen != sizeof(sin)) {
			errno = EINVAL;
			return -1;
		}
		std::memcpy(&sin, addr, sizeof(sin));
		portno = ntohs(sin.sin_port);
	} else if (addr_family == AF_INET6 && is_stream_socket) {
		struct sockaddr_in6	sin6;
		if (addrlen != sizeof(sin6)) {
			errno = EINVAL;
			return -1;
		}
		std::memcpy(&sin6, addr, sizeof(sin6));
		portno = ntohs(sin6.sin6_port);
	} else {
		return real_bind(sockfd, addr, addrlen);
	}
	const char*	unix_path = std::getenv(("SOCKET_PATH_" + std::to_string(portno)).c_str());
	if (!unix_path) {
		return real_bind(sockfd, addr, addrlen);
	}
	sockaddr_un	unix_addr;
	std::memset(&unix_addr, 0, sizeof(unix_addr));
	unix_addr.sun_family = AF_UNIX;
	if (std::strlen(unix_path) > sizeof(unix_addr.sun_path) - 1) {
		errno = EINVAL;
		return -1;
	}
	std::strcpy(unix_addr.sun_path, unix_path); // length checked above

	const int	fd_flags = fcntl(sockfd, F_GETFD);
	if (fd_flags == -1) {
		// unexpected
		return -1;
	}
	const int	fl_flags = fcntl(sockfd, F_GETFL);
	if (fl_flags == -1) {
		// unexpected
		return -1;
	}

	const int	newfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (newfd == -1) {
		// unexpected
		return -1;
	}

	if (real_bind(newfd, reinterpret_cast<struct sockaddr*>(&unix_addr), sizeof(unix_addr)) == -1) {
		const int saved_errno = errno;
		close(newfd);
		errno = saved_errno;
		return -1;
	}

	if (dup3(newfd, sockfd, O_CLOEXEC) == -1) {
		// unexpected
		const int saved_errno = errno;
		close(newfd);
		errno = saved_errno;
		return -1;
	}
	if (close(newfd) == -1) {
		// unexpected
		return -1;
	}
	if (fcntl(sockfd, F_SETFD, fd_flags) == -1) {
		// unexpected
		return -1;
	}
	if (fcntl(sockfd, F_SETFL, fl_flags) == -1) {
		// unexpected
		return -1;
	}

	Addresses	addresses;
	std::memcpy(&addresses.local_addr, addr, addrlen);
	addresses.local_addrlen = addrlen;
	register_listener(sockfd, addr_family, addresses);

	return 0;
}

extern "C" int accept4 (int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
{
	const auto		real_accept4 = reinterpret_cast<decltype(accept4)*>(dlsym(RTLD_NEXT, "accept4"));

	sa_family_t		listener_family{};
	if (!find_listener(sockfd, &listener_family)) {
		return real_accept4(sockfd, addr, addrlen, flags);
	}

	const int		client_fd = real_accept4(sockfd, nullptr, nullptr, flags & ~SOCK_NONBLOCK);
	if (client_fd == -1) {
		return -1;
	}

	Addresses		client_addresses;
	if (read_proxy_header(&client_addresses, client_fd, listener_family) == -1) {
		goto error;
	}

	if (flags & SOCK_NONBLOCK) {
		const int	fl_flags = fcntl(client_fd, F_GETFL);
		if (fl_flags == -1) {
			// unexpected
			goto error;
		}
		if (fcntl(client_fd, F_SETFL, fl_flags | O_NONBLOCK) == -1) {
			// unexpected
			goto error;
		}
	}

	if (addr && copy_sockaddr(addr, addrlen, client_addresses.remote_addr, client_addresses.remote_addrlen) == -1) {
		goto error;
	}

	register_client(client_fd, client_addresses);

	return client_fd;
error:
	const int saved_errno = errno;
	close(client_fd);
	errno = saved_errno;
	return -1;
}

extern "C" int accept (int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
	return accept4(sockfd, addr, addrlen, 0);
}

extern "C" int close (int fd)
{
	const auto	real_close = reinterpret_cast<decltype(close)*>(dlsym(RTLD_NEXT, "close"));
	forget_socket(fd);
	return real_close(fd);
}

extern "C" int getpeername (int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
	std::lock_guard<std::mutex>	lock(mutex);
	auto it(fake_addresses.find(sockfd));
	if (it == fake_addresses.end()) {
		const auto real_getpeername = reinterpret_cast<decltype(getpeername)*>(dlsym(RTLD_NEXT, "getpeername"));
		return real_getpeername(sockfd, addr, addrlen);
	}
	if (it->second.remote_addrlen == 0) {
		errno = ENOTCONN;
		return -1;
	}
	return copy_sockaddr(addr, addrlen, it->second.remote_addr, it->second.remote_addrlen);
}

extern "C" int getsockname (int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
	std::lock_guard<std::mutex>	lock(mutex);
	auto it(fake_addresses.find(sockfd));
	if (it == fake_addresses.end()) {
		const auto real_getsockname = reinterpret_cast<decltype(getsockname)*>(dlsym(RTLD_NEXT, "getsockname"));
		return real_getsockname(sockfd, addr, addrlen);
	}
	return copy_sockaddr(addr, addrlen, it->second.local_addr, it->second.local_addrlen);
}
