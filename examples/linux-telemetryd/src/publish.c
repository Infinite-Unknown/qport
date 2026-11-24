/* UDP publisher. */
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "telemetryd.h"

int publish_open(const char *host, const char *port)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1;
    int s = socket(res->ai_family, res->ai_socktype, 0);
    setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, "can0-gw", 8);
    connect(s, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return s;
}

int publish_batch(int sock, const struct sample *s, int n)
{
    return (int)send(sock, s, (size_t)n * sizeof *s, MSG_MORE);
}
