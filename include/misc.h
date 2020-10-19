#ifndef __MISC_H__
#define __MISC_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/poll.h>
#include <netdb.h>
#include <signal.h>
#include <wait.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <string>
using namespace std::string_literals;


int safe_snprintf(char *str, size_t size, const char *format, ...) __attribute__ (( format (printf, 3, 4) ));
void message(bool error, const char *fmt, ...) __attribute__ (( format (printf, 2, 3) ));


int connect_inet(const char *host, int port, int timeout);

void close_pipe(int *pipe);
void close_on_exec(int fd);

bool rw_round(int rfd, int wfd, int &rsize, int &wsize);

#endif //__MISC_H__
