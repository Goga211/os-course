#!/usr/bin/env bash
set -euo pipefail

IO="./io-load-ebpf"
LIB="$(readlink -f ../../vtpc/build/lib/libvtpc_ebpf.so)"
PROVIDER="vtpc_ebpf"

BS=4096
SIZE_MB=2048
FILE="./bench_file_2048mb.bin"
BLOCK_COUNT=$((SIZE_MB * 1024 * 1024 / BS))   # 524288

SEED=123456
REPEAT=3
DIRECT="off"
TYPE="random"

truncate -s "${SIZE_MB}M" "$FILE"

CMD=(
  "$IO"
  --rw read
  --block_size "$BS"
  --block_count "$BLOCK_COUNT"
  --file "$FILE"
  --direct "$DIRECT"
  --type "$TYPE"
  --repeat "$REPEAT"
  --seed "$SEED"
)

echo "[run] ${CMD[*]}"
echo "[lib] $LIB"
echo "[usdt] $PROVIDER:cache_hit / $PROVIDER:cache_miss"
echo

# 1) стартуем workload в фоне
"${CMD[@]}" &
APP_PID=$!

# 2) ждём, чтобы успела загрузиться .so
sleep 0.05

# 3) цепляемся bpftrace к PID и считаем
#    (uint64) чтобы убрать предупреждения signed/unsigned
sudo bpftrace -q -p "$APP_PID" -e "
BEGIN { @hits = 0; @misses = 0; }

usdt:$LIB:$PROVIDER:cache_hit  { @hits++; }
usdt:$LIB:$PROVIDER:cache_miss { @misses++; }

END {
  \$h = (uint64)@hits;
  \$m = (uint64)@misses;
  \$t = \$h + \$m;

  \$pct100 = (uint64)0;
  if (\$t > 0) { \$pct100 = (\$h * 10000) / \$t; }  // percent*100

  printf(\"\\n=== eBPF stats (USDT) ===\\n\");
  printf(\"hits=%llu misses=%llu hitrate=%llu.%02llu%%\\n\",
         \$h, \$m, \$pct100 / 100, \$pct100 % 100);
}
" &
BPF_PID=$!

# 4) ждём завершения workload
wait "$APP_PID"

# 5) стопаем bpftrace (чтобы сработал END)
sudo kill -INT "$BPF_PID" 2>/dev/null || true
wait "$BPF_PID" 2>/dev/null || true
