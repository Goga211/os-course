#!/usr/bin/env bash

echo "=== Starting CPU process test (16 procs) ==="

P=16
N=9223372036854775783
REPEAT=20

PIDS=()

for i in $(seq 1 $P); do
    echo "[*] Starting process $i"

    # stdout →cpu_proc_i.log
    # time → cpu_proc_i.time
    (
        /usr/bin/time -v ./build/bin/cpu-factorize \
            --n "$N" \
            --repeat "$REPEAT" \
            > cpu_proc_${i}.log 2> cpu_proc_${i}.time
    ) &
    PIDS+=($!)
done

echo "=== Waiting for all processes... ==="
for pid in "${PIDS[@]}"; do
    wait "$pid"
done

echo "=== All processes finished ==="
echo

echo "=========== TIME SUMMARY ==========="

for i in $(seq 1 $P); do
    t=$(grep "Elapsed (wall clock) time" cpu_proc_${i}.time | sed 's/.*: //')
    echo "Process $i : $t"
done

echo "===================================="

