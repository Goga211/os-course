#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <threads>"
    exit 1
fi

THREADS="$1"

# === WORKLOAD ===
N="9223372036854775783"
REPEAT_BASE=20
REPEAT=$((REPEAT_BASE * THREADS))

# === BIN ===
BIN="./build/bin/cpu-factorize-mt"

# === LOG DIR ===
TS=$(date +"%Y%m%d_%H%M%S")
LOG_DIR="cpu_thread_logs/p${THREADS}_${TS}"
mkdir -p "$LOG_DIR"

echo "=============================================="
echo "          CPU LOAD (Multithreaded)"
echo "=============================================="
echo "Threads        : $THREADS"
echo "N              : $N"
echo "Repeat base    : $REPEAT_BASE"
echo "Repeat scaled  : $REPEAT"
echo "Log dir        : $LOG_DIR"
echo "=============================================="
echo

echo ">>> RUNNING CPU LOAD..."
echo "----------------------------------------------"

# === START MONITORS ===

echo "[1] Starting iostat..."
IOSTAT_LOG="$LOG_DIR/iostat.txt"
iostat -x 1 > "$IOSTAT_LOG" &
PID_IOSTAT=$!

echo "[2] Starting top..."
TOP_LOG="$LOG_DIR/top.txt"
top -b -d 1 > "$TOP_LOG" &
PID_TOP=$!

# === RUN WORKLOAD ===
echo
echo "[3] Executing cpu-factorize-mt..."
TIME_LOG="$LOG_DIR/time.txt"

/usr/bin/time -v \
    "$BIN" \
    --threads "$THREADS" \
    --n "$N" \
    --repeat "$REPEAT" \
    2> "$TIME_LOG"

echo
echo ">>> CPU workload finished."
echo

# === STOP MONITORS ===
echo "[4] Stopping monitors..."
kill $PID_IOSTAT 2>/dev/null || true
kill $PID_TOP 2>/dev/null || true

# === SHOW OUTPUT LIKE IO SCRIPT ===

echo
echo "=== Elapsed time (from /usr/bin/time -v) ==="
grep -i "Elapsed" "$TIME_LOG"
echo
grep -i "User time" "$TIME_LOG"
grep -i "System time" "$TIME_LOG"
grep -i "Voluntary" "$TIME_LOG"
grep -i "Involuntary" "$TIME_LOG"
echo

echo "=== Last lines of iostat ==="
tail -n 10 "$IOSTAT_LOG"
echo

echo "=== Last lines of top ==="
tail -n 10 "$TOP_LOG"
echo

echo "=============================================="
echo "CPU MULTITHREAD TEST COMPLETED"
echo "Logs saved to: $LOG_DIR"
echo "=============================================="
