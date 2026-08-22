#pragma once

// Non-blocking UDP toward the plant's --setpoint-port. One datagram per call;
// EAGAIN is a drop, never a retry. Pre-loop setup only for open/close.
class SetpointTx {
  public:
    // Connects to 127.0.0.1:port with O_NONBLOCK. false on failure.
    bool open(int port);
    void close();

    // SET_POSITION_TARGET_LOCAL_NED, accel-only type mask. false if not sent.
    bool send_accel_setpoint(float ax, float ay, float az);

  private:
    int fd_ = -1;
};
