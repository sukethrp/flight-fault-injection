#!/usr/bin/env bash
# dummynet + pf on loopback. Teardown is pf first, then dnctl: flushing the
# pipe while a pf rule still points at it blackholes the port (rx_total=0).
set -euo pipefail

PORT="${PORT:-14555}"
PIPE="${PIPE:-1}"
PLR="${PLR:-0.15}"
DELAY_MS="${DELAY_MS:-}"

usage() {
  echo "usage: $0 {up|down|status} [options]" >&2
  echo "  env: PORT=$PORT PIPE=$PIPE PLR=$PLR DELAY_MS=${DELAY_MS:-unset}" >&2
  exit 2
}

cmd="${1:-}"
[[ -n "$cmd" ]] || usage
shift || true

case "$cmd" in
  up)
    # optional fixed delay in ms: dnctl pipe N config delay Xm
    if [[ -n "$DELAY_MS" ]]; then
      sudo dnctl pipe "$PIPE" config plr "$PLR" delay "${DELAY_MS}"
    else
      sudo dnctl pipe "$PIPE" config plr "$PLR"
    fi
    echo "dummynet in quick on proto udp from any to any port ${PORT} pipe ${PIPE}" \
      | sudo pfctl -f -
    sudo pfctl -e 2>/dev/null || true
    echo "netfault up: port=$PORT pipe=$PIPE plr=$PLR delay_ms=${DELAY_MS:-none}"
    ;;
  down)
    # pf first: stop referencing the pipe, then flush dummynet.
    sudo pfctl -d 2>/dev/null || true
    if [[ -f /etc/pf.conf ]]; then
      sudo pfctl -f /etc/pf.conf 2>/dev/null || sudo pfctl -F all 2>/dev/null || true
    else
      sudo pfctl -F all 2>/dev/null || true
    fi
    sudo dnctl -q flush
    echo "netfault down: pf then dnctl flushed"
    ;;
  status)
    echo "=== dnctl list ==="
    dnctl list 2>/dev/null || sudo dnctl list 2>/dev/null || echo "(dnctl unavailable)"
    echo "=== pfctl -s rules ==="
    sudo pfctl -s rules 2>/dev/null || echo "(pfctl rules unavailable)"
    echo "=== pfctl -s info (enabled?) ==="
    sudo pfctl -s info 2>/dev/null | head -20 || true
    ;;
  *)
    usage
    ;;
esac
