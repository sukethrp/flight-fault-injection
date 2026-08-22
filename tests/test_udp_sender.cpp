#include "udp_rx.h"

#include "common/mavlink.h"

#include <cstdint>
#include <cstdio>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static int fail(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    return 1;
}

int main() {
    const int fd = udp_bind_nonblocking(0);
    if (fd < 0) return fail("bind");
    sockaddr_in addr{};
    socklen_t alen = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
        close(fd);
        return fail("getsockname");
    }
    const int port = ntohs(addr.sin_port);
    char portstr[16];
    std::snprintf(portstr, sizeof(portstr), "%d", port);

    const pid_t pid = fork();
    if (pid < 0) { close(fd); return fail("fork"); }
    if (pid == 0) {
        execl(UDP_SENDER_PATH, "udp_sender",
              "--port", portstr,
              "--seconds", "1",
              "--imu-hz", "40",
              "--pos-hz", "10",
              "--gps-hz", "5",
              static_cast<char*>(nullptr));
        _exit(127);
    }

    mavlink_message_t msg{};
    mavlink_status_t st{};
    uint8_t buf[512];
    int n_imu = 0, n_pos = 0, n_gps = 0;
    bool child_done = false;
    int status = 0;
    for (;;) {
        const long n = udp_try_recv(fd, buf, sizeof(buf));
        if (n > 0) {
            for (long b = 0; b < n; ++b) {
                if (!mavlink_parse_char(MAVLINK_COMM_0, buf[b], &msg, &st)) continue;
                if (msg.msgid == MAVLINK_MSG_ID_HIGHRES_IMU) ++n_imu;
                else if (msg.msgid == MAVLINK_MSG_ID_LOCAL_POSITION_NED) ++n_pos;
                else if (msg.msgid == MAVLINK_MSG_ID_GPS_RAW_INT) ++n_gps;
            }
            continue;
        }
        if (!child_done) {
            const pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) child_done = true;
            else usleep(1000);
            continue;
        }
        break;
    }
    close(fd);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::fprintf(stderr, "udp_sender exit %d\n",
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return 1;
    }
    if (n_imu != 40 || n_pos != 10 || n_gps != 5) {
        std::fprintf(stderr, "counts imu=%d pos=%d gps=%d want 40/10/5\n",
                     n_imu, n_pos, n_gps);
        return 1;
    }
    return 0;
}
