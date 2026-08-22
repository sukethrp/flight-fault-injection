#include "ekf.h"

#include <cmath>

namespace {

constexpr float kGravity = 9.80665f;  // match plant::kGravity

}  // namespace

void mat_zero(float* a, int n) {
    for (int i = 0; i < n; ++i) a[i] = 0.0f;
}

void mat_ident(float* a, int n) {
    mat_zero(a, n * n);
    for (int i = 0; i < n; ++i) a[i * n + i] = 1.0f;
}

void mat_mul(const float* a, const float* b, float* c, int m, int n, int p) {
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < p; ++j) {
            float s = 0.0f;
            for (int k = 0; k < n; ++k) s += a[i * n + k] * b[k * p + j];
            c[i * p + j] = s;
        }
    }
}

void mat_transpose(const float* a, float* at, int rows, int cols) {
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) at[j * rows + i] = a[i * cols + j];
}

void mat_add(const float* a, const float* b, float* c, int n) {
    for (int i = 0; i < n; ++i) c[i] = a[i] + b[i];
}

void mat_sub(const float* a, const float* b, float* c, int n) {
    for (int i = 0; i < n; ++i) c[i] = a[i] - b[i];
}

void mat_scale(float* a, float s, int n) {
    for (int i = 0; i < n; ++i) a[i] *= s;
}

float mat_trace(const float* a, int n) {
    float t = 0.0f;
    for (int i = 0; i < n; ++i) t += a[i * n + i];
    return t;
}

Ekf::Ekf(EkfConfig cfg)
    : cfg_(cfg), last_nis_(0.0f), rejects_(0) {
    float x0[kN]{};
    float P0[kN] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    reset(x0, P0);
}

void Ekf::reset(const float x0[kN], const float P0_diag[kN]) {
    for (int i = 0; i < kN; ++i) x_[i] = x0[i];
    mat_zero(&P_[0][0], kN * kN);
    for (int i = 0; i < kN; ++i) P_[i][i] = P0_diag[i];
    build_H();
    build_R();
    // F/B/Q depend on dt; filled on the first predict.
    mat_ident(&F_[0][0], kN);
    mat_zero(&B_[0][0], kN * kU);
    mat_zero(&Q_[0][0], kN * kN);
    last_nis_ = 0.0f;
    rejects_  = 0;
}

void Ekf::build_F(float dt) {
    mat_ident(&F_[0][0], kN);
    // p' = p + v dt
    F_[0][3] = dt;
    F_[1][4] = dt;
    F_[2][5] = dt;
}

void Ekf::build_B(float dt) {
    mat_zero(&B_[0][0], kN * kU);
    const float dt2 = 0.5f * dt * dt;
    // p += 0.5 a dt² ; v += a dt
    B_[0][0] = dt2;
    B_[1][1] = dt2;
    B_[2][2] = dt2;
    B_[3][0] = dt;
    B_[4][1] = dt;
    B_[5][2] = dt;
}

void Ekf::build_Q(float dt) {
    // Discrete process noise from accel: Q = σ_a² B Bᵀ. B already encodes dt.
    build_B(dt);
    float Bt[kU][kN];
    mat_transpose(&B_[0][0], &Bt[0][0], kN, kU);
    float BB[kN][kN];
    mat_mul(&B_[0][0], &Bt[0][0], &BB[0][0], kN, kU, kN);
    const float s2 = cfg_.sigma_acc * cfg_.sigma_acc;
    for (int i = 0; i < kN * kN; ++i) (&Q_[0][0])[i] = s2 * (&BB[0][0])[i];
}

void Ekf::build_H() {
    mat_zero(&H_[0][0], kM * kN);
    H_[0][0] = 1.0f;
    H_[1][1] = 1.0f;
    H_[2][2] = 1.0f;
}

void Ekf::build_R() {
    mat_zero(&R_[0][0], kM * kM);
    const float s2 = cfg_.sigma_pos * cfg_.sigma_pos;
    R_[0][0] = s2;
    R_[1][1] = s2;
    R_[2][2] = s2;
}

float Ekf::trace_p() const {
    return mat_trace(&P_[0][0], kN);
}

void Ekf::predict(const float accel_body[3], float dt) {
    // AUTHOR: implement — NOTES.md 2026-08-22 (gate is detector + robustness).
    (void)accel_body;
    (void)dt;
    (void)kGravity;
}

bool Ekf::correct(const float pos_meas[3]) {
    // AUTHOR: implement — Joseph P=(I−KH)P(I−KH)ᵀ+KRKᵀ; short form dies on soak.
    (void)pos_meas;
    return false;
}
