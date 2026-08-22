#pragma once

#include <cstdint>

// Tunables live outside Ekf: clang rejects default args that need nested
// in-class initializers while the enclosing class is still incomplete.
struct EkfConfig {
    float sigma_acc = 0.5f;    // process accel noise, m/s² (feeds Q)
    float sigma_pos = 0.05f;   // position measurement noise, m (feeds R)
    // χ² 3 dof: 7.815 @95%, 11.345 @99%, 16.266 @99.9%. Joseph form required.
    float nis_gate  = 7.815f;
};

// 6-state NED position + velocity. Fixed-size float arrays only: Eigen::Dynamic
// heap-allocates, and this runs inside the 250 Hz loop.
class Ekf {
  public:
    static constexpr int kN = 6;  // [pn, pe, pd, vn, ve, vd]
    static constexpr int kM = 3;  // position measurement
    static constexpr int kU = 3;  // accel input

    explicit Ekf(EkfConfig cfg = {});

    // x0 / P0_diag length kN. P starts diagonal; off-diagonals cleared.
    void reset(const float x0[kN], const float P0_diag[kN]);

    // Discrete-time predict with body-frame specific force.
    // Must: build F and B from dt; subtract gravity from the D channel so the
    // integrator sees kinematic accel; x ← F x + B a; P ← F P Fᵀ + Q(dt).
    // Must not allocate, take locks, or I/O.
    void predict(const float accel_body[3], float dt);

    // Position measurement update (LOCAL_POSITION_NED).
    // Must: innovation y = z − H x; S = H P Hᵀ + R; NIS = yᵀ S⁻¹ y; if NIS
    // exceeds the gate, increment rejects and return false without touching
    // x or P; otherwise Kalman gain, state update, and Joseph-form
    // P ← (I−KH) P (I−KH)ᵀ + K R Kᵀ. Return true on accept.
    bool correct(const float pos_meas[3]);

    const float* state() const { return x_; }
    // Row-major kN×kN.
    const float* cov() const { return &P_[0][0]; }
    float        trace_p() const;
    float        last_nis() const { return last_nis_; }
    uint32_t     reject_count() const { return rejects_; }

  private:
    EkfConfig cfg_;
    float     x_[kN];
    float     P_[kN][kN];
    float     F_[kN][kN];
    float     B_[kN][kU];
    float     H_[kM][kN];
    float     Q_[kN][kN];
    float     R_[kM][kM];
    float     last_nis_;
    uint32_t  rejects_;

    void build_F(float dt);
    void build_B(float dt);
    void build_Q(float dt);
    void build_H();
    void build_R();
};

// Fixed-size helpers. Row-major a[R][C]. No heap.
void mat_zero(float* a, int n);
void mat_ident(float* a, int n);
void mat_mul(const float* a, const float* b, float* c, int m, int n, int p);
void mat_transpose(const float* a, float* at, int rows, int cols);
void mat_add(const float* a, const float* b, float* c, int n);
void mat_sub(const float* a, const float* b, float* c, int n);
void mat_scale(float* a, float s, int n);
float mat_trace(const float* a, int n);
