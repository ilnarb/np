#include "misc.h"

#include <vector>
#include <thread>
#include <atomic>

#define DUP2_STDOUT_2SOCKET 1

namespace
{
	std::atomic_bool stop{false};
	void stop_signal(int)
	{
		stop = true;
	}
} // namespace

void worker(int server_fd, int fd, const char *cmd)
{
#if !DUP2_STDOUT_2SOCKET
	int out[2];
	if (pipe(out) == -1)
	{
		message(true, "pipe failed");
		return;
	}
#endif
	//
	pid_t pid = fork();
	if (pid == -1)
	{
		message(true, "fork failed");
#if !DUP2_STDOUT_2SOCKET
		close_pipe(out);
#endif
		return;
	}
	if (pid == 0)
	{
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
		//
		close(server_fd);
		//
		if (dup2(fd, STDIN_FILENO) == -1)
			message(true, "dup2(%d, STDIN_FILENO) failed\n", fd);
		//
#if DUP2_STDOUT_2SOCKET
		if (dup2(fd, STDOUT_FILENO) == -1)
			message(true, "dup2(%d, STDOUT_FILENO) failed\n", fd);
#else
		if (dup2(out[1], STDOUT_FILENO) == -1)
			message(true, "dup2(%d, STDOUT_FILENO) failed\n", out[1]);
		close_pipe(out);
#endif
		//
		if (execl("/bin/sh", "sh", "-c", cmd, NULL) == -1)
			message(true, "execl(\"/bin/sh\", \"sh\", \"-c\", \"%s\", NULL) failed", cmd);
		// should never happen
		exit(1);
	}

	bool failed = false;
#if !DUP2_STDOUT_2SOCKET
	close(out[1]);
	close_on_exec(out[0]);

	while (!stop.load())
	{
		int rsize = 0, wsize = 0;
		if (!rw_round(out[0], fd, rsize, wsize))
		{
			failed = true;
			break;
		}
		if (rsize == 0)
			break;
	}
	close(out[0]);
#endif

	int status = 0;
	if (waitpid(pid, &status, 0) == 0)
	{
		// in case of fail send status using OOB data
		if (!WIFEXITED(status) || WEXITSTATUS(status))
		{
			char flag = 1;
			send(fd, &flag, 1, MSG_OOB);
		}
	}
	else if (stop || failed)
	{
		char flag = 1;
		send(fd, &flag, 1, MSG_OOB);
	}

	close(fd);
}

int server_main(int listen_port, const char *cmd)
{
	int server_fd;
	if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		return 1;
	}
	int onoff = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &onoff, sizeof(onoff)) < 0)
	{
		message(true, "Cannot setsockopt(%d, SOL_SOCKET, SO_REUSEADDR, %d)\n", server_fd, onoff);
	}

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = htons(listen_port);
	//
	if (bind(server_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
	{
		message(true, "Cannot bind\n");
		close(server_fd);
		return 1;
	}
	if (listen(server_fd, 10) < 0)
	{
		message(true, "Cannot listen %d\n", server_fd);
		close(server_fd);
		return 1;
	}

	signal(SIGINT, stop_signal);
	signal(SIGQUIT, stop_signal);
	signal(SIGTERM, stop_signal);
	signal(SIGPIPE, SIG_IGN);

	std::vector<std::thread> threads;
	while (!stop.load())
	{
		struct sockaddr_in sa;
		socklen_t sa_len = sizeof(sa);

		struct pollfd fds;
		fds.fd = server_fd;
		fds.revents = 0;
		fds.events = POLLIN | POLLRDNORM | POLLHUP | POLLERR | POLLNVAL;
		if (poll(&fds, 1, 1000) <= 0)
			continue;

		int fd = accept(server_fd, (struct sockaddr *)&sa, &sa_len);
		if (fd >= 0)
		{
			threads.emplace_back(worker, server_fd, fd, cmd);
		}
	}

	for (auto &th : threads)
		th.join();

	return 0;
}
