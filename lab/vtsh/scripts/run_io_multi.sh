#!/usr/bin/env bash
set -euo pipefail

# ============ Аргументы ============
if [ $# -lt 8 ]; then
    echo "Usage: $0 <instances> <file> <block_size> <block_count> <repeat> <rw> <type> <direct>"
    echo "Example:"
    echo "  $0 16 test.dat 4096 50000 1 read sequence off"
    echo "  $0 16 test.dat 4096 50000 1 read sequence on"
    exit 1
fi

INSTANCES="$1"
FILE="$2"
BS="$3"
BC="$4"
REPEAT="$5"
RW="$6"
PICK="$7"      # sequence | random
DIRECT="$8"    # on | off

BIN="./build/bin/io-load"
LOG_DIR="io_multi_logs"
mkdir -p "$LOG_DIR"

CPU=$(nproc)

echo "========================================"
echo "          IO MULTI-PROCESS RUN"
echo "----------------------------------------"
echo " File        = $FILE"
echo " Block size  = $BS"
echo " Block count = $BC"
echo " Repeat      = $REPEAT"
echo " RW          = $RW"
echo " Pick        = $PICK"
echo " Direct      = $DIRECT"
echo " Instances   = $INSTANCES"
echo " CPU cores   = $CPU"
echo "========================================"


# ============ 1. Запуск процессов ============
PIDS=()

echo "=== [1] Запуск $INSTANCES процессов ==="
for i in $(seq 1 $INSTANCES); do
    $BIN \
        --file "$FILE" \
        --block_size "$BS" \
        --block_count "$BC" \
        --repeat "$REPEAT" \
        --rw "$RW" \
        --type "$PICK" \
        --direct "$DIRECT" &

    PIDS+=("$!")
done

echo "PIDs: ${PIDS[*]}"

PID_LIST=$(printf ",%s" "${PIDS[@]}")
PID_LIST="${PID_LIST:1}"


# ============ 2. pidstat ============
echo
echo "=== [2] pidstat ==="

PIDSTAT_LOG="$LOG_DIR/pidstat.txt"

pidstat -w -p "$PID_LIST" 1 > "$PIDSTAT_LOG" &
PIDSTAT_PID=$!


# ============ 3. iostat ============
echo
echo "=== [3] iostat ==="

IOSTAT_LOG="$LOG_DIR/iostat.txt"
iostat -x 1 3 > "$IOSTAT_LOG" &
IOSTAT_PID=$!


# ============ Ожидание всех процессов ============
for PID in "${PIDS[@]}"; do
    wait "$PID"
done

sleep 1
kill "$PIDSTAT_PID" >/dev/null 2>&1 || true
kill "$IOSTAT_PID" >/dev/null 2>&1 || true

echo
echo "=== Все экземпляры завершены ==="

echo "Последние строки pidstat:"
tail -n 20 "$PIDSTAT_LOG"

echo
echo "Последние строки iostat:"
tail -n 12 "$IOSTAT_LOG"

echo
echo "========================================"
echo "Логи сохранены в:"
echo "  $LOG_DIR/"
echo "========================================"
