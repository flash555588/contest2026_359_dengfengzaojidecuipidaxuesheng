/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "claw_connect.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int remaining_ms(int64_t deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -errno;
    int64_t remaining = deadline - ((int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
    if (remaining <= 0) return 0;
    return remaining > INT_MAX ? INT_MAX : (int)remaining;
}

int claw_connect_tcp(const char *host, const char *port, unsigned int timeout_ms)
{
    struct addrinfo hints = {0}, *addresses = NULL;
    struct timespec now;
    int result = -ECONNREFUSED;
    if (!host || !*host || !port || !*port || !timeout_ms || timeout_ms > INT_MAX)
        return -EINVAL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    int lookup = getaddrinfo(host, port, &hints, &addresses);
    if (lookup) return lookup == EAI_SYSTEM ? -errno : -EHOSTUNREACH;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        result = -errno;
        goto done;
    }
    int64_t deadline = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 + timeout_ms;
    for (struct addrinfo *address = addresses; address; address = address->ai_next) {
        int remaining = remaining_ms(deadline);
        if (remaining <= 0) { result = remaining ? remaining : -ETIMEDOUT; break; }
        int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) { result = -errno; continue; }
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            result = -errno;
            close(fd);
            continue;
        }
        int rc = connect(fd, address->ai_addr, address->ai_addrlen);
        int error = rc == 0 ? 0 : errno;
        if (error == EINPROGRESS || error == EWOULDBLOCK || error == EINTR) {
            struct pollfd descriptor = {.fd = fd, .events = POLLOUT};
            for (;;) {
                remaining = remaining_ms(deadline);
                if (remaining <= 0) { error = remaining ? -remaining : ETIMEDOUT; break; }
                rc = poll(&descriptor, 1, remaining);
                if (rc < 0 && errno == EINTR) continue;
                if (rc <= 0) { error = rc == 0 ? ETIMEDOUT : errno; break; }
                if (descriptor.revents & POLLNVAL) { error = EBADF; break; }
                socklen_t size = sizeof(error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size)) error = errno;
                break;
            }
        }
        if (!error && fcntl(fd, F_SETFL, flags) < 0) error = errno;
        if (!error) { result = fd; goto done; }
        result = -error;
        close(fd);
    }
done:
    freeaddrinfo(addresses);
    return result;
}
