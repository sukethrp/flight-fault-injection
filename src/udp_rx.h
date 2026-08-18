#pragma once
#include <cstddef>

// Bind a non-blocking UDP socket on a loopback port. -1 on failure.
// O_NONBLOCK is set here, once, so the hot path never passes MSG_DONTWAIT and
// never has to reason about blocking.
int udp_bind_nonblocking(int port);

// One datagram, or -1 with nothing available. Never blocks.
long udp_try_recv(int fd, void* buf, size_t cap);
