#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/poll.h>
#include <netdb.h>
#include <wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <vector>
#include <thread>
#include <atomic>

std::atomic_bool stop = false;
void stop_signal(int)
{
	stop = true;
}

void close_pipe(int *pipe)
{
	close(pipe[0]);
	close(pipe[1]);
}
void close_on_exec(int fd)
{
	int flags = fcntl(fd, F_GETFD, 0);
	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
		fprintf(stderr, "process_t: fcntl(%d, F_SETFD, CLOEXEC) error: %d %s\n", fd, errno, strerror(errno));
}

void worker(int server_fd, int fd, const char *cmd)
{
	int out[2];
	if (pipe(out) == -1)
	{
		fprintf(stderr, "pipe failed: %d (%s)\n", errno, strerror(errno));
		return;
	}
	//
	pid_t pid = fork();
	if (pid == -1)
	{
		fprintf(stderr, "fork failed: %d (%s)\n", errno, strerror(errno));
		close_pipe(out);
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
			fprintf(stderr, "process_t: dup2(%d, STDIN_FILENO) error: %d %s\n", fd, errno, strerror(errno));
		//		if (dup2(out[1], STDOUT_FILENO) == -1)
		if (dup2(fd, STDOUT_FILENO) == -1)
			fprintf(stderr, "process_t: dup2(%d, STDOUT_FILENO) error: %d %s\n", out[1], errno, strerror(errno));
		close_pipe(out);
		//
		if (execl("/bin/sh", "sh", "-c", cmd, NULL) == -1)
			fprintf(stderr, "execl(\"/bin/sh\", \"sh\", \"-c\", \"%s\", NULL) error: %d %s\n", cmd, errno, strerror(errno));
		// should never happen
		exit(0);
	}

	close(out[1]);
	close_on_exec(out[0]);
	/*
	char buf[8*1024];
	while(!stop.load())
	{
		int n1 = read(out[0], buf, sizeof(buf));
		if (n1 < 0 && errno == EINTR) continue;
		if (n1 <= 0) break;

		bool failed = false;
		int wr = 0;
		do
		{
			int n2 = write(fd, buf + wr, n1 - wr);
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
		if (failed)
			break;
	}
*/
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

	close(out[0]);
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
		fprintf(stderr, "Cannot setsockopt(%d, SOL_SOCKET, SO_REUSEADDR, %d)\n", server_fd, onoff);
	}

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = htons(listen_port);
	//
	if (bind(server_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
	{
		fprintf(stderr, "Cannot bind\n");
		close(server_fd);
		return 1;
	}
	if (listen(server_fd, 10) < 0)
	{
		fprintf(stderr, "Cannot listen %d\n", server_fd);
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
