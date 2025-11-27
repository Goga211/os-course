#!/usr/bin/env bash
set -euo pipefail

# Папка для логов
LOG_DIR="ema_mt_logs"
mkdir -p "$LOG_DIR"

if [ $# -lt 5 ]; then
    echo "Usage: $0 <left> <right> <out> <repeat> <threads>"
    exit 1
fi

LEFT="$1"
RIGHT="$2"
OUT="$3"
REPEAT="$4"
THREADS="$5"

BIN="./build/bin/ema-join-nl-mt"

echo "========================================"
echo "      EMA JOIN (Multithreaded)"
echo "----------------------------------------"
echo " Left table  : $LEFT"
echo " Right table : $RIGHT"
echo " Output      : $OUT"
echo " Repeat      : $REPEAT"
echo " Threads     : $THREADS"
echo "========================================"


###############################################
# 1. /usr/bin/time -v
###############################################
echo
echo "=== [1] /usr/bin/time -v ==="

TIME_LOG="$LOG_DIR/time.txt"
/usr/bin/time -v $BIN \
    --left "$LEFT" \
    --right "$RIGHT" \
    --out "$OUT" \
    --repeat "$REPEAT" \
    --threads "$THREADS" \
    2> "$TIME_LOG"

echo "Краткое резюме:"
grep -E "User time|System time|Elapsed|Maximum" "$TIME_LOG" || true


###############################################
# 2. pidstat (context switches)
###############################################
echo
echo "=== [2] pidstat (context switches) ==="

PIDSTAT_LOG="$LOG_DIR/pidstat.txt"

# Запускаем EMA join в фоне
$BIN --left "$LEFT" --right "$RIGHT" --out "$OUT" \
     --repeat "$REPEAT" --threads "$THREADS" &

PID=$!

echo "PID процесса: $PID"

# мониторинг каждую секунду
pidstat -w -p $PID 1 > "$PIDSTAT_LOG" &

wait $PID

echo "Последние строки pidstat:"
tail -n 20 "$PIDSTAT_LOG"


###############################################
# 3. iostat (I/O нагрузка)
###############################################
echo
echo "=== [3] iostat ==="

IOSTAT_LOG="$LOG_DIR/iostat.txt"

iostat -x 1 2 > "$IOSTAT_LOG" || true

echo "Последние строки iostat:"
tail -n 15 "$IOSTAT_LOG"


###############################################
# Итог
###############################################
echo
echo "========================================"
echo " Логи сохранены в:"
echo "   $LOG_DIR/time.txt"
echo "   $LOG_DIR/pidstat.txt"
echo "   $LOG_DIR/iostat.txt"
echo "========================================"
