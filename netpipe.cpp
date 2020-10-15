#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/poll.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <thread>

int connect_inet(const char *host, int port, int timeout);
int server_main(int listen_port, const char *cmd);

int main(int argc, char *argv[])
{
	int ch;
	int listen_port = -1;

	while((ch = getopt(argc, argv, "l:c:")) >= 0)
	{
		switch(ch)
		{
		case 'l':
			listen_port = atoi(optarg);
			break;
		}
	}

	if (listen_port > 0)
	{
		if (argc - optind > 0)
		{
			std::string cmd = argv[optind];
			for(int i = optind + 1; i < argc; i++)
			{
				cmd += " ";
				cmd += argv[i];
			}
			server_main(listen_port, cmd.c_str());
			return 0;
		}
		return EXIT_FAILURE;
	}

	if (argc < 3)
		return EXIT_FAILURE;

	const char *host = argv[1];
	int port = atoi(argv[2]);

	// client
	int fd = connect_inet(host, port, 5000);
	if (fd)
	{
		std::thread thout([&]{
			while(true)
			{
				char buf[8*1024];
				int n = read(fd, buf, sizeof(buf));
				if (n < 0 && errno == EINTR) continue;
				if (n <= 0) break;
				fprintf(stdout, buf, n);
			}
		});
		//
		while(!feof(stdin))
		{
			char buf[8*1024];
			int n1 = read(fileno(stdin), buf, sizeof(buf));
			if (n1 < 0 && errno == EINTR) continue;
			if (n1 <= 0) break;

			bool err = false;
			int wr = 0;
			do
			{
				int n2 = write(fd, buf + wr, n1 - wr);
				if (n2 < 0 && errno == EINTR) continue;
				if (n2 <= 0) { err = true; break; }
				wr += n2;
			}
			while (wr < n1);
			if (err) break;
		}
		//
		shutdown(fd, SHUT_WR);
		thout.join();
		close(fd);
	}

	return 0;
}

int connect_inet(const char *host, int port, int timeout)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // Stream socket type
	hints.ai_protocol = IPPROTO_TCP; // TCP proto

	char service[32];
	sprintf(service, "%d", port);

	struct addrinfo *addrinfo = NULL;
	int err = getaddrinfo(host, service, &hints, &addrinfo);
	if (err != 0)
	{
		fprintf(stderr, "Cannot resolve address %s:%s: %d (%s)\n", host, service, err, gai_strerror(err));
		return false;
	}

	struct call_freeaddrinfo_t {
		struct addrinfo *&_addrinfo;
		call_freeaddrinfo_t(struct addrinfo *&addrinfo): _addrinfo(addrinfo) {}
		~call_freeaddrinfo_t() { if (_addrinfo) { freeaddrinfo(_addrinfo); _addrinfo = NULL; } }
	}
	call_freeaddrinfo(addrinfo);

	int fd = -1;
	for(auto p = addrinfo; p != NULL; p = p->ai_next)
	{
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd == -1)
		{
			// Protocol is not supported on the host
			fprintf(stderr, "failed to create socket: %d (%s)\n", errno, strerror(errno));
			continue;
		}

		// Enable non-blocking I/O
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags == -1)
		{
			fprintf(stderr, "switching to async mode failed: %d (%s)\n", errno, strerror(errno));
			close(fd); fd = -1;
			continue;
		}
		//set nonblocing mode to use timeout while connecting
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		{
			fprintf(stderr, "switching to async mode failed: %d (%s)\n", errno, strerror(errno));
			close(fd); fd = -1;
			continue;
		}

		// pending connect request
		if (connect(fd, p->ai_addr, p->ai_addrlen) < 0
			&& errno != EINPROGRESS && errno != EINTR)
		{
			fprintf(stderr, "connect error: %d (%s)\n", errno, strerror(errno));
			close(fd); fd = -1;
			continue;
		}

		struct pollfd fds;
		fds.fd = fd;
		fds.revents = 0;
		fds.events = POLLOUT | POLLWRNORM;
		int ret = poll(&fds, 1, timeout);
		if (ret == 0)
		{
			fprintf(stderr, "connect timeout expired\n");
			close(fd); fd = -1;
			continue;
		}
		if (ret < 0)
		{
			fprintf(stderr, "connect error: %d (%s)\n", errno, strerror(errno));
			close(fd); fd = -1;
			continue;
		}
		//
		int err = 0;
		socklen_t len = sizeof(err);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
		{
			fprintf(stderr, "getsockopt failed: %d (%s)\n", errno, strerror(errno));
			close(fd); fd = -1;
			continue;
		}
		if (err)
		{
			fprintf(stderr, "cant connect: %d (%s)\n", err, strerror(err));
			close(fd); fd = -1;
			continue;
		}

		// return to blocking mode
		if (fcntl(fd, F_SETFL, flags) < 0)
		{
			fprintf(stderr, "cant switch to blocking mode: %d (%s)\n", err, strerror(err));
			close(fd); fd = -1;
			continue;
		}

		break;
	}

	return fd;
}
