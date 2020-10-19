#include "misc.h"

int safe_snprintf(char *str, size_t size, const char *format, ...)
{
    if (size == 1)
        str[0] = 0;
    if (size <= 1)
        return 0;
    //
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(str, size, format, ap);
    va_end(ap);
    //
    if (n < 0)
        return 0;
    //
    if ((size_t)n >= size)
    {
        str[size - 1] = 0;
        return size - 1;
    }
    return n;
}

void message(bool error, const char *format, ...)
{
    int log = STDERR_FILENO;
    int hold_errno = errno;

    if (log < 0)
        return;

    char string[4 * 1024];
    const int max_string = sizeof(string) - 1; // -1 for trailing \n

    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(string, max_string, format, ap);
    va_end(ap);

    if (n < 0)
        return;
    if ((size_t)n >= max_string)
    {
        string[max_string - 1] = 0;
        n = max_string - 1;
    }
    else
    {
        if (error)
            n += safe_snprintf(string + n, max_string - n, ": %d (%s)", hold_errno, strerror(hold_errno));
    }

    if (n < max_string)
    {
        if (string[n - 1] != '\n')
        {
            string[n++] = '\n';
            string[n] = 0;
        }
    }
    else
    {
        string[max_string - 1] = '\n';
        string[max_string] = 0;
    }

    if (n > max_string)
        n = max_string;

    write(log, string, n);
}

int connect_inet(const char *host, int port, int timeout)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // Stream socket type
    hints.ai_protocol = IPPROTO_TCP; // TCP proto

    char service[32];
    sprintf(service, "%d", port);

    struct addrinfo *addrinfo = NULL;
    int err = getaddrinfo(host, service, &hints, &addrinfo);
    if (err != 0)
    {
        message(false, "Cannot resolve address %s:%s: %d (%s)\n", host, service, err, gai_strerror(err));
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
        if (fd >= 0)
        {
            close(fd);
            fd = -1;
        }

        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1)
        {
            // Protocol is not supported on the host
            message(true, "failed to create socket");
            continue;
        }

        // Enable non-blocking I/O
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            message(true, "switching to async mode failed");
            continue;
        }
        // set nonblocing mode to use timeout while connecting
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            message(true, "switching to async mode failed");
            continue;
        }

        // pending connect request
        if (connect(fd, p->ai_addr, p->ai_addrlen) < 0 && errno != EINPROGRESS && errno != EINTR)
        {
            message(true, "connect error");
            continue;
        }

        struct pollfd fds;
        fds.fd = fd;
        fds.revents = 0;
        fds.events = POLLOUT | POLLWRNORM;
        int ret = poll(&fds, 1, timeout);
        if (ret == 0)
        {
            message(true, "connect timeout expired");
            continue;
        }
        if (ret < 0)
        {
            message(true, "poll error");
            continue;
        }
        //
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        {
            message(true, "getsockopt failed");
            continue;
        }
        if (err)
        {
            message(true, "cant connect");
            continue;
        }

        // return to blocking mode
        if (fcntl(fd, F_SETFL, flags) < 0)
        {
            message(true, "cant switch to blocking mode");
            continue;
        }

        return fd;
    }

    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
    return -1;
}

void close_pipe(int *pipe)
{
    close(pipe[0]);
    close(pipe[1]);
}

void close_on_exec(int fd)
{
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags == -1)
    {
        message(true, "fcntl F_GETFD failed");
        return;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
    {
        message(true, "fcntl F_SETFD FD_CLOEXEC failed");
    }
}

bool rw_round(int rfd, int wfd, int &rsize, int &wsize)
{
    char buf[8 * 1024];
    rsize = read(rfd, buf, sizeof(buf));
    if (rsize < 0)
        return (errno == EINTR);
    if (rsize == 0)
        return true;

    wsize = 0;
    do
    {
        int n = write(wfd, buf + wsize, rsize - wsize);
        if (n < 0)
            return (errno == EINTR);
        if (n > 0)
            wsize += n;
        else
            break;
    }
    while (wsize < rsize);

    return true;
}
