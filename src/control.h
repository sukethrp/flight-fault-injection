#pragma once

#include <cstdint>

// Cascaded position → velocity → acceleration. Gains and amax are fixed for
// now; Phase 5 fault runs are where the anti-windup and D-on-measurement
// choices show up in the recovery transient.
struct AccelCmd {
    float ax;
    float ay;
    float az;
};

struct ControllerGains {
    float kp_pos = 1.2f;   // (m/s) / m
    float kp_vel = 3.0f;   // (m/s²) / (m/s)
    float ki_vel = 0.8f;   // (m/s²) / (m·s)
    float kd_vel = 0.15f;  // (m/s²) / (m/s²) of measured vdot
};

class Controller {
  public:
    // amax matches plant::kAmaxDefault. saturating past it freezes the
    // velocity integrator so a GPS dropout cannot wind the recovery worse
    // than the fault.
    explicit Controller(float amax = 20.0f, ControllerGains g = {});

    AccelCmd update(const float pos[3], const float vel[3],
                    const float pos_sp[3], float dt);

    void reset();

  private:
    ControllerGains gains_;
    float           amax_;
    float           integ_[3];
    float           vel_prev_[3];
    bool            have_vel_prev_;
};
