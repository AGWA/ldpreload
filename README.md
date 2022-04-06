# Andrew's LD_PRELOAD libraries

To compile, run `make`.

## ldpreload-forcerdonly.so

Convert read/write `open` calls to read-only.  Useful for programs that open a file read/write but never write to it.

## ldpreload-forceurandom.so

Convert `open` calls of `/dev/random` to `/dev/urandom`, because [`/dev/urandom` on Linux is secure](https://www.2uo.de/myths-about-urandom/) while (on old kernels) `/dev/random` is stupidly slow.

## ldpreload-prebind.so

Intercepts `bind` calls for particular TCP or UDP port numbers and replaces them with a `dup3` call from a file descriptor of your choosing.  This is useful for making an off-the-shelf server program listen on a pre-bound file descriptor that you pass to it.  For example, a privileged supervisor process could bind to a privileged port like 443, drop privileges, and then launch the server process with the pre-bound socket on file descriptor 3.  Although some server programs support listening on a pre-bound file descriptor, most do not, requiring the use of `ldpreload-prebind.so`.

The following environment variables are used to configure `ldpreload-prebind.so` (replace `443` with the port number and `3` with the prebound file descriptor number):

* `TCPFD_443=3` for a TCP port
* `UDPFD_443=3` for a UDP port

## ldpreload-unixbind.so

Intercepts `bind` calls for particular TCP/IP port numbers, and replaces them with a `bind` to a UNIX domain socket of your choosing.  When a connection is accepted on the socket, the local and remote IP addresses are read from the connection using the the [PROXY protocol v2](https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt), and made available via `getpeername`, `getsockname`, and the `addr` argument to `accept`.

The following environment variable is used to configure `ldpreload-unixbind.so` (replace `443` with the port number and `/path/to/socket` with the path to the UNIX domain socket):

* `SOCKET_PATH_443=/path/to/socket`
