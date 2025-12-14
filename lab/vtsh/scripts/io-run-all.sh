#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <P=threads_and_processes>"
    exit 1
fi

P="$1"

BIN_PROC="./build/bin/io-load"
BIN_MT="./build/bin/io-load-mt"

FILE="test.dat"
BS=4096
BCOUNT=50000
REPEAT=10

RW_MODES=("read" "write")
DIRECT_MODES=("off" "on")

TS=$(date +"%Y%m%d_%H%M%S")
BASE_DIR="io_tests_${TS}"
mkdir -p "$BASE_DIR"

echo "=============================================="
echo "           IO TESTS (Processes + Threads)"
echo "=============================================="
echo "P = $P"
echo "File = $FILE"
echo "Block size = $BS"
echo "Block count = $BCOUNT"
echo "Repeat = $REPEAT"
echo "Log dir = $BASE_DIR"
echo "=============================================="
echo


###########################################
#  FUNCTION: RUN PROCESS VERSION (P PROCS)
###########################################
run_process_version() {
    local RW="$1"
    local DIRECT="$2"

    local MODE_DIR="$BASE_DIR/proc_${RW}_direct_${DIRECT}"
    mkdir -p "$MODE_DIR"

    echo
    echo "=== PROCESS VERSION: RW=$RW DIRECT=$DIRECT P=$P ==="

    # Start monitors
    echo "[iostat] starting..."
    iostat -x 1 > "$MODE_DIR/iostat.txt" &
    PID_IOSTAT=$!

    echo "[top] starting..."
    top -b -d 1 > "$MODE_DIR/top.txt" &
    PID_TOP=$!

    PIDS=()

    # Run P processes
    for i in $(seq 1 "$P"); do
        /usr/bin/time -v \
            "$BIN_PROC" \
                --rw "$RW" \
                --direct "$DIRECT" \
                --block_size "$BS" \
                --block_count "$BCOUNT" \
                --file "$FILE" \
                --repeat "$REPEAT" \
            2> "$MODE_DIR/time_${i}.txt" &
        PIDS+=($!)
    done

    # Wait all
    for pid in "${PIDS[@]}"; do wait "$pid"; done

    kill $PID_IOSTAT 2>/dev/null || true
    kill $PID_TOP 2>/dev/null || true

    echo ">>> DONE."

    # Print merged time
    echo "--- TIME SUMMARY ---"
    grep "Elapsed" "$MODE_DIR"/time_*.txt
    grep "User time" "$MODE_DIR"/time_*.txt | head -1
    grep "System time" "$MODE_DIR"/time_*.txt | head -1
    grep "Involuntary context switches" "$MODE_DIR"/time_*.txt | head -1

    echo "--- LAST iostat ---"
    tail -n 10 "$MODE_DIR/iostat.txt"

    echo "--- LAST top ---"
    tail -n 10 "$MODE_DIR/top.txt"

    echo "============================================"
}


run_thread_version() {
    local RW="$1"
    local DIRECT="$2"

    local MODE_DIR="$BASE_DIR/thread_${RW}_direct_${DIRECT}"
    mkdir -p "$MODE_DIR"

    echo
    echo "=== THREAD VERSION: RW=$RW DIRECT=$DIRECT THREADS=$P ==="

    iostat -x 1 > "$MODE_DIR/iostat.txt" &
    PID_IOSTAT=$!

    top -b -d 1 > "$MODE_DIR/top.txt" &
    PID_TOP=$!

    /usr/bin/time -v \
        "$BIN_MT" \
            --rw "$RW" \
            --direct "$DIRECT" \
            --block_size "$BS" \
            --block_count "$BCOUNT" \
            --file "$FILE" \
            --threads "$P" \
            --repeat "$REPEAT" \
        2> "$MODE_DIR/time.txt"

    kill $PID_IOSTAT 2>/dev/null || true
    kill $PID_TOP 2>/dev/null || true

    echo ">>> DONE."

    echo "--- TIME ---"
    grep "Elapsed" "$MODE_DIR/time.txt"
    grep "User time" "$MODE_DIR/time.txt"
    grep "System time" "$MODE_DIR/time.txt"
    grep "Involuntary context switches" "$MODE_DIR/time.txt"

    echo "--- LAST iostat ---"
    tail -n 10 "$MODE_DIR/iostat.txt"

    echo "--- LAST top ---"
    tail -n 10 "$MODE_DIR/top.txt"

    echo "============================================"
}




###########################################
# RUN ALL 4 MODES FOR PROC + THREADS
###########################################

for RW in "${RW_MODES[@]}"; do
    for DIRECT in "${DIRECT_MODES[@]}"; do

        run_process_version "$RW" "$DIRECT"
        run_thread_version "$RW" "$DIRECT"

        echo "--------------------------------------------"

    done
done

echo
echo "=============================================="
echo "ALL IO TESTS COMPLETED"
echo "Logs saved to: $BASE_DIR"
echo "=============================================="
