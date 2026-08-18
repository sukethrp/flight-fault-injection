#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/.."
for i in 1 2 3 4 5 6; do
  echo "=== pair $i timeshare $(date +%T) ==="
  ./build/loop_bench --hz 250 --seconds 600 --warmup 2000 --load-us 500 \
      --label macos-timeshare --out results/hour_ts_$i.csv
  echo "=== pair $i rt $(date +%T) ==="
  ./build/loop_bench --hz 250 --seconds 600 --warmup 2000 --load-us 500 --rt \
      --label macos-rt --out results/hour_rt_$i.csv
done
echo "=== done $(date +%T) ==="
