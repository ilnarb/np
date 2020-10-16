#include <stdarg.h>
#include <errno.h>
#include <signal.h>
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
#include <atomic>

int connect_inet(const char *host, int port, int timeout);
int server_main(int listen_port, const char *cmd);

int sock = -1;
int flag = 0;
void sigurg(int)
{
	int n = recv(sock, &flag, sizeof(flag), MSG_OOB);
//	if (n < 0) flag = 0;
	signal(SIGURG, sigurg);
}

int main(int argc, char *argv[])
{
	int ch;
	int listen_port = -1;

	while ((ch = getopt(argc, argv, "l:")) >= 0)
	{
		switch (ch)
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
			for (int i = optind + 1; i < argc; i++)
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

	std::atomic_bool failed = false;
	// client
	sock = connect_inet(host, port, 5000);
	if (sock < 0)
		return EXIT_FAILURE;

	// want to recv exit status using OOB data
	fcntl(sock, F_SETOWN, getpid());
	signal(SIGURG, sigurg);

	//
	std::thread thout([&] {
		while (!failed.load())
		{
			char buf[8 * 1024];
			int n = recv(sock, buf, sizeof(buf), 0);
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				failed = true;
				break;
			}
			if (n > 0)
				fwrite(buf, 1, n, stdout);
			else
				break;
		}
	});
	//
	while (!feof(stdin) && !failed.load())
	{
		char buf[8 * 1024];
		int n1 = read(fileno(stdin), buf, sizeof(buf));
		if (n1 < 0)
		{
			if (errno == EINTR)
				continue;
			failed = true;
			break;
		}
		if (n1 == 0)
			break;

		int wr = 0;
		do
		{
			int n2 = send(sock, buf + wr, n1 - wr, 0);
			if (n2 < 0)
			{
				if (errno == EINTR)
					continue;
				failed = true;
				break;
			}
			if (n2 > 0)
				wr += n2;
			else
				break;
		}
		while (wr < n1);
	}
	//
	shutdown(sock, SHUT_WR);
	thout.join();
	//
	close(sock);
	//
	if (failed || flag)
		return EXIT_FAILURE;

	return 0;
}

int connect_inet(const char *host, int port, int timeout)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;	 // IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // Stream socket type
	hints.ai_protocol = IPPROTO_TCP; // TCP proto

	char service[32];
	sprintf(service, "%d", port);

	struct addrinfo *addrinfo = NULL;
	int err = getaddrinfo(host, service, &hints, &addrinfo);
	if (err != 0)
	{
		fprintf(stderr, "Cannot resolve address %s:%s: %d (%s)\n", host, service, err, gai_strerror(err));
		return -1;
	}

	struct call_freeaddrinfo_t
	{
		struct addrinfo *&_addrinfo;
		call_freeaddrinfo_t(struct addrinfo *&addrinfo) : _addrinfo(addrinfo) {}
		~call_freeaddrinfo_t()
		{
			if (_addrinfo)
			{
				freeaddrinfo(_addrinfo);
				_addrinfo = NULL;
			}
		}
	} call_freeaddrinfo(addrinfo);

	int fd = -1;
	for (auto p = addrinfo; p != NULL; p = p->ai_next)
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
			close(fd);
			fd = -1;
			continue;
		}
		//set nonblocing mode to use timeout while connecting
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		{
			fprintf(stderr, "switching to async mode failed: %d (%s)\n", errno, strerror(errno));
			close(fd);
			fd = -1;
			continue;
		}

		// pending connect request
		if (connect(fd, p->ai_addr, p->ai_addrlen) < 0 && errno != EINPROGRESS && errno != EINTR)
		{
			fprintf(stderr, "connect error: %d (%s)\n", errno, strerror(errno));
			close(fd);
			fd = -1;
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
			close(fd);
			fd = -1;
			continue;
		}
		if (ret < 0)
		{
			fprintf(stderr, "connect error: %d (%s)\n", errno, strerror(errno));
			close(fd);
			fd = -1;
			continue;
		}
		//
		int err = 0;
		socklen_t len = sizeof(err);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
		{
			fprintf(stderr, "getsockopt failed: %d (%s)\n", errno, strerror(errno));
			close(fd);
			fd = -1;
			continue;
		}
		if (err)
		{
			fprintf(stderr, "cant connect: %d (%s)\n", err, strerror(err));
			close(fd);
			fd = -1;
			continue;
		}

		// return to blocking mode
		if (fcntl(fd, F_SETFL, flags) < 0)
		{
			fprintf(stderr, "cant switch to blocking mode: %d (%s)\n", err, strerror(err));
			close(fd);
			fd = -1;
			continue;
		}

		break;
	}

	return fd;
}
