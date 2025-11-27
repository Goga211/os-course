#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 4 ]; then
    echo "Usage: $0 <n_value> <repeat> <threads> <instances>"
    exit 1
fi

N="$1"
REPEAT="$2"
THREADS="$3"
INSTANCES="$4"

BIN="./build/bin/cpu-factorize-mt"

LOG_DIR="cpu_mt_multi_logs"
mkdir -p "$LOG_DIR"


PIDS=()

# Запускаем N экземпляров cpu-factorize-mt
echo
echo "=== [1] запуск $INSTANCES экземпляров ==="

for i in $(seq 1 "$INSTANCES"); do
    LOG_INST="$LOG_DIR/time_$i.txt"
    echo "Запуск экземпляра $i..."
    /usr/bin/time -v $BIN --n "$N" --repeat "$REPEAT" --threads "$THREADS" \
        2> "$LOG_INST" &
    PIDS+=($!)
done

echo "PIDs: ${PIDS[*]}"

# pidstat
echo
echo "[2] pidstat"

PIDSTAT_LOG="$LOG_DIR/pidstat.txt"
PID_LIST=$(printf ",%s" "${PIDS[@]}")
PID_LIST="${PID_LIST:1}"

pidstat -w -p "$PID_LIST" 1 > "$PIDSTAT_LOG" &
PID_PIDSTAT=$!

# iostat
echo
echo "[3] iostat"

IOSTAT_LOG="$LOG_DIR/iostat.txt"
iostat -x 1 10 > "$IOSTAT_LOG" &
PID_IOSTAT=$!

# Ждём завершения всех экземпляров
for pid in "${PIDS[@]}"; do
    wait "$pid"
done

kill $PID_PIDSTAT 2>/dev/null || true
kill $PID_IOSTAT 2>/dev/null || true


echo "Последние строки pidstat:"
tail -n 15 "$PIDSTAT_LOG"

echo
echo "Последние строки iostat:"
tail -n 10 "$IOSTAT_LOG"

