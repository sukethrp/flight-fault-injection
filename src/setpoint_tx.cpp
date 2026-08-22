#include "setpoint_tx.h"

#include "common/mavlink.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// Ignore position, velocity, yaw, yaw_rate. Accel axes active; not FORCE_SET.
constexpr uint16_t kAccelOnlyMask = static_cast<uint16_t>(
    POSITION_TARGET_TYPEMASK_X_IGNORE | POSITION_TARGET_TYPEMASK_Y_IGNORE |
    POSITION_TARGET_TYPEMASK_Z_IGNORE | POSITION_TARGET_TYPEMASK_VX_IGNORE |
    POSITION_TARGET_TYPEMASK_VY_IGNORE | POSITION_TARGET_TYPEMASK_VZ_IGNORE |
    POSITION_TARGET_TYPEMASK_YAW_IGNORE | POSITION_TARGET_TYPEMASK_YAW_RATE_IGNORE);

constexpr uint8_t kSysId  = 1;
constexpr uint8_t kCompId = 1;

}

bool SetpointTx::open(int port) {
    if (port <= 0) return false;
    if (fd_ >= 0) close();

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(static_cast<uint16_t>(port));
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // connect fixes the peer so send() works; the plant binds this port.
    if (::connect(fd, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) != 0) {
        ::close(fd);
        return false;
    }
    if (::fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        ::close(fd);
        return false;
    }
    fd_ = fd;
    return true;
}

void SetpointTx::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SetpointTx::send_accel_setpoint(float ax, float ay, float az) {
    if (fd_ < 0) return false;

    mavlink_message_t msg{};
    mavlink_msg_set_position_target_local_ned_pack(
        kSysId, kCompId, &msg,
        /*time_boot_ms=*/0,
        /*target_system=*/1,
        /*target_component=*/1,
        MAV_FRAME_LOCAL_NED,
        kAccelOnlyMask,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        ax, ay, az,
        0.0f, 0.0f);

    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    const uint16_t n = mavlink_msg_to_send_buffer(buf, &msg);
    const ssize_t sent = ::send(fd_, buf, n, 0);
    if (sent < 0) {
        // EAGAIN/EWOULDBLOCK: drop this tick rather than block the loop.
        return false;
    }
    return static_cast<uint16_t>(sent) == n;
}
