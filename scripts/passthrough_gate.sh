#!/usr/bin/env bash
# One-shot A/B: direct baseline vs idle proxy. 60 s loop each.
set -u
cd "$(dirname "$0")/.."
mkdir -p results

clear_ports() {
  pkill -9 -f './build/plant' 2>/dev/null || true
  pkill -9 -f './build/proxy' 2>/dev/null || true
  pkill -9 -f './build/loop_bench' 2>/dev/null || true
  sleep 0.5
  for p in 14550 14551 14555 14556; do
    pids=$(lsof -t -nP -iUDP:$p 2>/dev/null || true)
    [[ -n "${pids:-}" ]] && kill -9 $pids 2>/dev/null || true
  done
  sleep 0.5
}

run_direct() {
  echo "=== BASELINE direct $(date +%T) ==="
  ./build/plant --seconds 80 --sensor-port 14555 --setpoint-port 14556 \
    --seed 1 --acc-noise 0 --gyro-noise 0 --pos-noise 0 --gps-noise 0 \
    --acc-bias-n 0 --acc-bias-e 0 --acc-bias-d 0 \
    --truth-out results/p5_base_truth.csv --rt \
    > results/p5_base_plant.err 2>&1 &
  local plant=$!
  sleep 1
  ./build/loop_bench --hz 250 --seconds 60 --warmup 2000 --rt --ekf \
    --port 14555 --setpoint-port 14556 \
    --label p5-base --out results/p5_base.csv \
    > results/p5_base_loop.err 2>&1
  local rc=$?
  wait "$plant" 2>/dev/null || true
  echo "baseline loop_rc=$rc"
  cat results/p5_base_loop.err
}

run_passthrough() {
  echo "=== PASSTHROUGH $(date +%T) ==="
  ./build/proxy --listen-plant 14550 --forward-loop 14555 --listen-loop 14551 --forward-plant 14556 \
    --seconds 95 --event-out results/p5_pt_events.csv --label p5-passthrough \
    > results/p5_pt_proxy.err 2>&1 &
  local proxy=$!
  sleep 0.5
  ./build/plant --seconds 80 --sensor-port 14550 --setpoint-port 14556 \
    --seed 1 --acc-noise 0 --gyro-noise 0 --pos-noise 0 --gps-noise 0 \
    --acc-bias-n 0 --acc-bias-e 0 --acc-bias-d 0 \
    --truth-out results/p5_pt_truth.csv --rt \
    > results/p5_pt_plant.err 2>&1 &
  local plant=$!
  sleep 1
  ./build/loop_bench --hz 250 --seconds 60 --warmup 2000 --rt --ekf \
    --port 14555 --setpoint-port 14551 \
    --label p5-passthrough --out results/p5_pt.csv \
    > results/p5_pt_loop.err 2>&1
  local rc=$?
  wait "$plant" 2>/dev/null || true
  wait "$proxy" 2>/dev/null || true
  echo "passthrough loop_rc=$rc"
  cat results/p5_pt_loop.err
  cat results/p5_pt_proxy.err
}

clear_ports
# baseline already has a good loop CSV; only re-run if truth missing
if [[ ! -s results/p5_base_truth.csv ]]; then
  clear_ports
  run_direct
fi
clear_ports
run_passthrough
echo "=== artifacts ==="
ls -la results/p5_base.csv results/p5_base_truth.csv results/p5_pt.csv results/p5_pt_truth.csv results/p5_pt_events.csv 2>&1
echo "=== done $(date +%T) ==="
