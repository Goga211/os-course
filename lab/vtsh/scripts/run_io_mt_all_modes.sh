#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "Usage: $0 <threads>"
    exit 1
fi

THREADS="$1"

# базовые параметры (как в процессной версии)
FILE="test.dat"
BS=4096
BC=50000           # столько же блоков, как при 1 процессе
REP_BASE=10        # повторений на 1 поток

# увеличиваем только repeat, чтобы объем работы совпадал
REP=$((REP_BASE * THREADS))

MON="./scripts/run_io_mt_monitor.sh"

CONFIGS=(
    "read on"
    "read off"
    "write on"
    "write off"
)

echo "========================================"
echo "     RUNNING ALL 4 IO-MT CONFIGS"
echo "----------------------------------------"
echo " Threads: $THREADS"
echo " Block size:  $BS"
echo " Block count: $BC   (fixed)"
echo " Repeat:      $REP  (= $REP_BASE * $THREADS)"
echo "========================================"

for cfg in "${CONFIGS[@]}"; do
    read RW DIRECT <<< "$cfg"

    echo
    echo ">>> RUN: RW=$RW, DIRECT=$DIRECT, THREADS=$THREADS"
    echo "----------------------------------------"

    $MON "$FILE" "$BS" "$BC" "$REP" "$RW" "$THREADS" "$DIRECT"
done

echo
echo "========================================"
echo "       ALL MODES COMPLETE"
echo " Logs: io_mt_logs/"
echo "========================================"
