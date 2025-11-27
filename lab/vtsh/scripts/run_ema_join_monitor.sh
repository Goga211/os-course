#!/usr/bin/env bash
set -euo pipefail

CORES=${4:-$(nproc)}
BIN="./build/bin/ema-join-nl"

if [[ $# -lt 3 ]]; then
  echo "Usage: $0 <Lfile> <Rfile> <outfile> [instances]"
  exit 1
fi

LFILE="$1"
RFILE="$2"
OUT_BASE="$3"
INSTANCES="$CORES"

LOG_DIR="ema_multi_logs"
mkdir -p "$LOG_DIR"

echo "  EMA MULTI RUN"
echo "  L = $LFILE"
echo "  R = $RFILE"
echo "  OUT_BASE = $OUT_BASE"
echo "  INSTANCES = $INSTANCES"

PIDS=()

for i in $(seq 1 "$INSTANCES"); do
  OUT="${OUT_BASE}.${i}.txt"
  $BIN --left "$LFILE" --right "$RFILE" --out "$OUT" --repeat 1 &
  PIDS+=("$!")
done

echo "PIDs: ${PIDS[*]}"

pidstat -w -p "$(printf "%s," "${PIDS[@]}")" 1 > "$LOG_DIR/pidstat.txt" &
iostat -x 1 3 > "$LOG_DIR/iostat.txt" &

for PID in "${PIDS[@]}"; do
    wait "$PID"
done
