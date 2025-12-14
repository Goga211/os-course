#!/usr/bin/env bash
set -euo pipefail

THREADS=32
BIN="./build/bin/io-load-mt"

FILE="test.dat"
BS=4096
BCOUNT=50000
REPEAT=10

TS=$(date +"%Y%m%d_%H%M%S")
LOG_DIR="io_thread_logs/p${THREADS}_${TS}"
mkdir -p "$LOG_DIR"

echo "=============================================="
echo "          IO LOAD (Multithreaded)"
echo "=============================================="
echo "Threads       : $THREADS"
echo "Block size    : $BS"
echo "Block count   : $BCOUNT"
echo "Repeat        : $REPEAT"
echo "=============================================="

# monitors
echo "[1] Starting iostat..."
IOSTAT_LOG="$LOG_DIR/iostat.txt"
iostat -x 1 > "$IOSTAT_LOG" &
PID_IOSTAT=$!

echo "[2] Starting top..."
TOP_LOG="$LOG_DIR/top.txt"
top -b -d 1 > "$TOP_LOG" &
PID_TOP=$!

echo
echo "[3] Running workload..."

TIME_LOG="$LOG_DIR/time.txt"

/usr/bin/time -v \
    "$BIN" \
    --threads "$THREADS" \
    --rw read \
    --block_size "$BS" \
    --block_count "$BCOUNT" \
    --file "$FILE" \
    --repeat "$REPEAT" \
    2> "$TIME_LOG"

echo
echo ">>> IO thread workload finished."
echo

# stop monitors
echo "[4] Stopping monitors..."
kill $PID_IOSTAT 2>/dev/null || true
kill $PID_TOP 2>/dev/null || true

echo "Logs saved to: $LOG_DIR"
