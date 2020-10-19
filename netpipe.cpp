#include "misc.h"

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

	while ((ch = getopt(argc, argv, "hl:")) >= 0)
	{
		switch (ch)
		{
		case 'l':
			listen_port = atoi(optarg);
			break;
		case 'h':
			printf("usage:\n");
			printf("\t%s -l port cmd\n", argv[0]);
			printf("\t%s host port\n", argv[0]);
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
			int rsize = 0, wsize = 0;
			if (!rw_round(sock, STDOUT_FILENO, rsize, wsize))
			{
				failed = true;
				break;
			}
			if (rsize == 0)
				break;
		}
	});
	//
	while(!failed.load())
	{
		int rsize = 0, wsize = 0;
		if (!rw_round(STDIN_FILENO, sock, rsize, wsize))
		{
			failed = true;
			break;
		}
		if (rsize == 0)
			break;
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
