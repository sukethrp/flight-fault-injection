#include "failsafe.h"

Failsafe::Failsafe(FailsafeConfig cfg, FailsafeEventLog* log)
    : cfg_(cfg), log_(log) {
    reset();
}

void Failsafe::reset() {
    state_            = FailsafeState::NOMINAL;
    event_seq_        = 0;
    confirm_degraded_ = 0;
    confirm_safe_     = 0;
    confirm_locked_   = 0;
    confirm_nominal_  = 0;
    entered_ns_       = 0;
    last_eval_ns_     = 0;
}

void Failsafe::transition_to(FailsafeState next, const FailsafeInput& in) {
    if (next == state_) return;
    FailsafeEvent e{};
    // stamp with the evaluate clock so TTR shares the injector epoch.
    e.mono_ns  = in.mono_ns != 0 ? in.mono_ns : rt::now_ns();
    e.from     = static_cast<uint8_t>(state_);
    e.to       = static_cast<uint8_t>(next);
    e.det_mask = in.det_mask;
    e.seq      = event_seq_++;
    if (log_) log_->push(e);
    state_      = next;
    entered_ns_ = e.mono_ns;
    confirm_degraded_ = 0;
    confirm_safe_     = 0;
    confirm_locked_   = 0;
    confirm_nominal_  = 0;
}

void Failsafe::evaluate(const FailsafeInput& in) {
    last_eval_ns_ = in.mono_ns;

    // AUTHOR: implement — NOTES.md 2026-08-22. N-consecutive confirm +
    // asymmetric hold_*_ns; a detector that chatters is worse than none.
    // Predicates: which DET_* / nis_rejects / est_err_m / trace_p enter
    // DEGRADED vs SAFE vs LOCKED. Do not author the policy from an agent.
    (void)cfg_;
    (void)confirm_degraded_;
    (void)confirm_safe_;
    (void)confirm_locked_;
    (void)confirm_nominal_;
    (void)entered_ns_;
    (void)in;
}
