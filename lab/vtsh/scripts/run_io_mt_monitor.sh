#!/usr/bin/env bash
set -euo pipefail

LOG_DIR="io_mt_logs"
mkdir -p "$LOG_DIR"

if [ $# -lt 7 ]; then
    echo "Usage: $0 <file> <block_size> <block_count> <repeat> <rw> <threads> <direct on|off>"
    echo "Example:"
    echo "  $0 test.dat 4096 50000 10 read 16 on"
    exit 1
fi

FILE="$1"
BS="$2"
BC="$3"
REPEAT="$4"
RW="$5"          # read/write
THREADS="$6"
DIRECT="$7"      # on/off

BIN="./build/bin/io-load-mt"

echo "========================================"
echo "        IO LOAD (Multithreaded)"
echo "----------------------------------------"
echo " File        : $FILE"
echo " Block size  : $BS"
echo " Block count : $BC"
echo " Repeat      : $REPEAT"
echo " RW mode     : $RW"
echo " Threads     : $THREADS"
echo " Direct I/O  : $DIRECT"
echo " Log dir     : $LOG_DIR"
echo "========================================"
echo


###############################
# 1. /usr/bin/time -v
###############################
echo "=== [1] /usr/bin/time -v ==="

TIME_LOG="$LOG_DIR/time_${RW}_thr${THREADS}_direct-${DIRECT}.txt"

/usr/bin/time -v \
    $BIN \
        --file "$FILE" \
        --block_size "$BS" \
        --block_count "$BC" \
        --repeat "$REPEAT" \
        --rw "$RW" \
        --threads "$THREADS" \
        --direct "$DIRECT" \
    2> "$TIME_LOG" > /dev/null

echo "Краткое резюме:"
grep -E "User time|System time|Elapsed|Maximum" "$TIME_LOG" || true



###############################
# 2. pidstat
###############################
echo
echo "=== [2] pidstat (context switches) ==="

PIDSTAT_LOG="$LOG_DIR/pidstat_${RW}_thr${THREADS}_direct-${DIRECT}.txt"

$BIN \
    --file "$FILE" \
    --block_size "$BS" \
    --block_count "$BC" \
    --repeat "$REPEAT" \
    --rw "$RW" \
    --threads "$THREADS" \
    --direct "$DIRECT" &

PID=$!
echo "PID процесса: $PID"

pidstat -w -p "$PID" 1 > "$PIDSTAT_LOG" &

wait $PID

echo "Последние строки pidstat:"
tail -n 15 "$PIDSTAT_LOG"



###############################
# 3. iostat
###############################
echo
echo "=== [3] iostat ==="

IOSTAT_LOG="$LOG_DIR/iostat_${RW}_thr${THREADS}_direct-${DIRECT}.txt"
iostat -x 1 2 > "$IOSTAT_LOG" || true

echo "Последние строки iostat:"
tail -n 10 "$IOSTAT_LOG"
