#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

static int get_socket_type (int sockfd, int* type)
{
	socklen_t optlen = sizeof(*type);
	return getsockopt(sockfd, SOL_SOCKET, SO_TYPE, type, &optlen);
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

	int		socket_type;
	if (get_socket_type(sockfd, &socket_type) == -1) {
		return -1;
	}

	int		portno{};
	if (addr_family == AF_INET && socket_type == SOCK_STREAM) {
		struct sockaddr_in	sin;
		if (addrlen != sizeof(sin)) {
			errno = EINVAL;
			return -1;
		}
		std::memcpy(&sin, addr, sizeof(sin));
		portno = ntohs(sin.sin_port);
	} else if (addr_family == AF_INET6) {
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

	std::string	env_name;
	if (socket_type == SOCK_STREAM) {
		env_name = "TCPFD_" + std::to_string(portno);
	} else if (socket_type == SOCK_DGRAM) {
		env_name = "UDPFD_" + std::to_string(portno);
	} else {
		return real_bind(sockfd, addr, addrlen);
	}

	const char*	env_value = std::getenv(env_name.c_str());
	if (!env_value) {
		return real_bind(sockfd, addr, addrlen);
	}

	const int	prebound_fd = std::atoi(env_value);

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

	if (dup3(prebound_fd, sockfd, O_CLOEXEC) == -1) {
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
	return 0;
}
