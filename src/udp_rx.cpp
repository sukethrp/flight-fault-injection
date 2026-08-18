#include "udp_rx.h"

#include <arpa/inet.h>
#include <cstdint>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

int udp_bind_nonblocking(int port) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    sockaddr_in me{};
    me.sin_family      = AF_INET;
    me.sin_port        = htons(static_cast<uint16_t>(port));
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0) { close(fd); return -1; }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0) { close(fd); return -1; }
    return fd;
}

long udp_try_recv(int fd, void* buf, size_t cap) {
    return recv(fd, buf, cap, 0);
}
