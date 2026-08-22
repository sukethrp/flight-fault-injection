#!/usr/bin/env bash
# Eleven faults × 50 reps, 600 s chunks, identical plant seed / trajectory.
# One clean passthrough control per session. Skip any CSV that already exists.
# Re-invoke under nohup+caffeinate+disown unless FAULT_CAMPAIGN_INNER=1.
set -u
cd "$(dirname "$0")/.."

OUTDIR=${OUTDIR:-results/fault_campaign}
SECONDS_PER=${SECONDS_PER:-600}
REPS=${REPS:-50}
HZ=250
WARMUP=2000
PLANT_SEED=1

# plant → proxy → loop on these ports (setpoints reverse through proxy).
LISTEN_PLANT=14550
FORWARD_LOOP=14555
LISTEN_LOOP=14551
FORWARD_PLANT=14556

FAULTS=(
  imu_dropout
  pos_dropout
  gps_dropout
  packet_drop
  packet_delay
  reorder
  imu_bitflip
  imu_bias
  imu_stuck
  gps_jump
  clock_skew
)

if [[ -z "${FAULT_CAMPAIGN_INNER:-}" ]]; then
  mkdir -p "$OUTDIR"
  export FAULT_CAMPAIGN_INNER=1
  export OUTDIR SECONDS_PER REPS
  nohup caffeinate -dims "$0" "$@" >>"$OUTDIR/campaign.log" 2>&1 &
  echo "fault_campaign started pid $! log=$OUTDIR/campaign.log"
  disown
  exit 0
fi

mkdir -p "$OUTDIR" injector/configs

need_bin() {
  if [[ ! -x $1 ]]; then
    echo "missing $1; cmake -B build && cmake --build build" >&2
    exit 1
  fi
}

need_bin ./build/plant
need_bin ./build/loop_bench
need_bin ./build/proxy

chunk_done() {
  # resumable: accept .csv or .csv.gz with ≥90% of expected sample rows
  local csv=$1
  if [[ -s ${csv}.gz ]]; then
    csv=${csv}.gz
  elif [[ ! -s $csv ]]; then
    return 1
  fi
  local n
  if [[ $csv == *.gz ]]; then
    n=$(gzip -dc "$csv" | grep -v '^#' | wc -l | tr -d ' ')
  else
    n=$(grep -v '^#' "$csv" | wc -l | tr -d ' ')
  fi
  local expect=$((HZ * SECONDS_PER))
  [[ $n -gt $((expect * 9 / 10)) ]]
}

run_chunk() {
  local label=$1
  local config=$2   # empty = passthrough
  local loop_csv=$3
  local truth_csv=$4
  local event_csv=$5
  local seed=$6

  if chunk_done "$loop_csv"; then
    echo "=== skip $label (exists) $(date +%T) ==="
    return 0
  fi

  echo "=== $label $(date +%T) ==="

  local proxy_args=(
    --listen-plant "$LISTEN_PLANT"
    --forward-loop "$FORWARD_LOOP"
    --listen-loop "$LISTEN_LOOP"
    --forward-plant "$FORWARD_PLANT"
    --seconds "$((SECONDS_PER + 30))"
    --seed "$seed"
    --label "$label"
    --event-out "$event_csv"
  )
  if [[ -n $config ]]; then
    proxy_args+=(--config "$config")
  fi

  ./build/proxy "${proxy_args[@]}" >"$OUTDIR/${label}_proxy.err" 2>&1 &
  local proxy_pid=$!

  ./build/plant \
    --seconds "$((SECONDS_PER + 15))" \
    --sensor-port "$LISTEN_PLANT" \
    --setpoint-port "$FORWARD_PLANT" \
    --seed "$PLANT_SEED" \
    --acc-noise 0 --gyro-noise 0 --pos-noise 0 --gps-noise 0 \
    --acc-bias-n 0 --acc-bias-e 0 --acc-bias-d 0 \
    --truth-out "$truth_csv" \
    --rt \
    >"$OUTDIR/${label}_plant.err" 2>&1 &
  local plant_pid=$!

  # plant and proxy bind first; brief settle before the loop joins.
  sleep 1

  ./build/loop_bench \
    --hz "$HZ" \
    --seconds "$SECONDS_PER" \
    --warmup "$WARMUP" \
    --rt \
    --ekf \
    --port "$FORWARD_LOOP" \
    --setpoint-port "$LISTEN_LOOP" \
    --label "$label" \
    --out "$loop_csv" \
    >"$OUTDIR/${label}_loop.err" 2>&1
  local loop_rc=$?

  kill "$plant_pid" "$proxy_pid" 2>/dev/null || true
  wait "$plant_pid" 2>/dev/null || true
  wait "$proxy_pid" 2>/dev/null || true

  if [[ $loop_rc -ne 0 ]]; then
    echo "loop_bench failed rc=$loop_rc for $label" >&2
    return "$loop_rc"
  fi
  gzip -f "$loop_csv" 2>/dev/null || true
  gzip -f "$truth_csv" 2>/dev/null || true
  gzip -f "$event_csv" 2>/dev/null || true
}

# session clean control: drift check against the Phase 3 closed-loop baseline.
run_chunk "clean_control" "" \
  "$OUTDIR/clean_control.csv" \
  "$OUTDIR/clean_control_truth.csv" \
  "$OUTDIR/clean_control_events.csv" \
  1

for fault in "${FAULTS[@]}"; do
  cfg="injector/configs/${fault}.conf"
  if [[ ! -f $cfg ]]; then
    echo "missing config $cfg" >&2
    exit 1
  fi
  for ((r = 1; r <= REPS; r++)); do
    rep=$(printf '%02d' "$r")
    label="${fault}_r${rep}"
    # per-rep seed: reproducible phase draw, different across the matrix.
    seed=$((1000 + r * 17 + ${#fault} * 1009))
    # rewrite seed into a temp config so phase RNG matches the chunk label.
    tmpcfg="$OUTDIR/${label}.conf"
    {
      echo "seed=$seed"
      grep -v '^seed=' "$cfg" || true
    } >"$tmpcfg"
    run_chunk "$label" "$tmpcfg" \
      "$OUTDIR/${label}.csv" \
      "$OUTDIR/${label}_truth.csv" \
      "$OUTDIR/${label}_events.csv" \
      "$seed"
  done
done

echo "=== done $(date +%T) ==="
