set -euo pipefail

BUILD_BIN="${BUILD_BIN:-$(cd "$(dirname "$0")/.."; pwd)/build/bin}"
IO="$BUILD_BIN/io-load"
OUT_ROOT="runs/$(date +%Y%m%d-%H%M%S)"
TEST_FILE="${TEST_FILE:-$OUT_ROOT/test.img}"

BS="${BS:-4096}"
REPEAT="${REPEAT:-1}"
SEED="${SEED:-42}"
PREALLOC_GB="${PREALLOC_GB:-16}"

TOP_DELAY="${TOP_DELAY:-0.2}"
TOP_ITERS="${TOP_ITERS:-1200}"

BC_READ_SEQ_OFF=25446484
BC_READ_RAND_OFF=15422277
BC_WRITE_SEQ_OFF=11475410
BC_WRITE_RAND_OFF=4684845
BC_READ_SEQ_ON=712541
BC_WRITE_SEQ_ON=514265

need() { command -v "$1" >/dev/null 2>&1 || { echo "need: $1"; exit 1; }; }
[[ -x "$IO" ]] || { echo "binary not found: $IO"; exit 1; }
need /usr/bin/time
need iostat
need top
need awk
need sed
command -v pidstat >/dev/null 2>&1 || echo "warn: pidstat not found (install: sudo apt install sysstat)"

mkdir -p "$OUT_ROOT"

get_child_of() {
  local p="$1" ch=""
  for _ in {1..60}; do
    if [[ -r "/proc/$p/task/$p/children" ]]; then
      ch="$(cat "/proc/$p/task/$p/children")" || true
      [[ -n "$ch" ]] && { echo "$ch" | awk '{print $1}'; return 0; }
    fi
    sleep 0.05
  done
  echo "$p"
}

start_mon() {
  local pid="$1" dir="$2"
  iostat -m 1 >"$dir/iostat.txt" 2>&1 & echo $! >"$dir/iostat.pid"
  top -b -d "$TOP_DELAY" -n "$TOP_ITERS" -p "$pid" >"$dir/top.txt" 2>&1 & echo $! >"$dir/top.pid"
  if command -v pidstat >/dev/null 2>&1; then
    pidstat -rud -p "$pid" 1 >"$dir/pidstat_cpu.txt" 2>&1 & echo $! >"$dir/pidstat_cpu.pid"
    pidstat -w   -p "$pid" 1 >"$dir/pidstat_csw.txt" 2>&1 & echo $! >"$dir/pidstat_csw.pid"
  fi
}
stop_mon() {
  local dir="$1"
  for f in iostat.pid top.pid pidstat_cpu.pid pidstat_csw.pid; do
    [[ -f "$dir/$f" ]] && kill "$(cat "$dir/$f")" >/dev/null 2>&1 || true
  done
}

prealloc_file() {
  local bytes=$(( PREALLOC_GB * 1024 * 1024 * 1024 ))
  mkdir -p "$(dirname "$TEST_FILE")"
  if [[ ! -f "$TEST_FILE" ]] || [[ "$(stat -c%s "$TEST_FILE" 2>/dev/null || echo 0)" -lt "$bytes" ]]; then
    if command -v fallocate >/dev/null 2>&1; then
      fallocate -l "$bytes" "$TEST_FILE" || dd if=/dev/zero of="$TEST_FILE" bs=1M count=$((PREALLOC_GB*1024)) status=progress
    else
      dd if=/dev/zero of="$TEST_FILE" bs=1M count=$((PREALLOC_GB*1024)) status=progress
    fi
  fi
}

run_case() {
  local tag="$1"; shift
  local dir="$OUT_ROOT/$tag"; mkdir -p "$dir"

  echo "==> $tag"
  echo "CMD: $IO $*" | tee "$dir/cmd.txt"

  ( exec /usr/bin/time -v -o "$dir/time.txt" "$IO" "$@" ) &
  local time_pid=$!
  sleep 0.1
  local child_pid; child_pid="$(get_child_of "$time_pid")"

  start_mon "$child_pid" "$dir"
  local t0=$(date +%s%N)
  wait "$time_pid" || true
  local rc=$?
  local t1=$(date +%s%N)
  stop_mon "$dir"

  local dur_ms=$(( (t1 - t0) / 1000000 ))
  {
    echo "RC=$rc, duration=${dur_ms}ms"
    echo "--- iostat (last 10) ---"
    tail -10 "$dir/iostat.txt" || true
    echo "--- top (last 20) ---"
    tail -20 "$dir/top.txt" || true
    if [[ -f "$dir/pidstat_cpu.txt" ]]; then
      echo "--- pidstat cpu (last 10) ---"
      tail -10 "$dir/pidstat_cpu.txt" || true
      echo "--- pidstat csw (last 10) ---"
      tail -10 "$dir/pidstat_csw.txt" || true
    fi
    echo "--- time summary (/usr/bin/time -v) ---"
    grep -E 'Elapsed|User time|System time|Maximum resident set|Major|Minor|voluntary|involuntary' "$dir/time.txt" || true
  } | tee "$dir/summary.txt"

  echo "Artifacts: $dir"
  echo
}

main() {
  prealloc_file
  local FILE_SIZE; FILE_SIZE=$(stat -c%s "$TEST_FILE")
  local HI_ON=$(( (FILE_SIZE / BS) * BS ))
  local HI_RAND=$(( FILE_SIZE < 8589934592 ? FILE_SIZE : 8589934592 ))
  HI_RAND=$(( (HI_RAND / BS) * BS )); [[ $HI_RAND -lt $BS ]] && HI_RAND=$BS

  run_case "read_seq_off" \
    --rw read --block_size "$BS" --block_count "$BC_READ_SEQ_OFF" --repeat "$REPEAT" \
    --file "$TEST_FILE" --range 0-0 --direct off --type sequence --seed "$SEED"

  run_case "read_rand_off" \
    --rw read --block_size "$BS" --block_count "$BC_READ_RAND_OFF" --repeat "$REPEAT" \
    --file "$TEST_FILE" --range 0-0 --direct off --type random --seed "$SEED"

  run_case "write_seq_off" \
    --rw write --block_size "$BS" --block_count "$BC_WRITE_SEQ_OFF" --repeat "$REPEAT" \
    --file "$TEST_FILE" --range 0-0 --direct off --type sequence --seed "$SEED"

  run_case "write_rand_off" \
    --rw write --block_size "$BS" --block_count "$BC_WRITE_RAND_OFF" --repeat "$REPEAT" \
    --file "$TEST_FILE" --range 0-"$HI_RAND" --direct off --type random --seed "$SEED"

  run_case "read_seq_on" \
    --rw read --block_size "$BS" --block_count "$BC_READ_SEQ_ON" --repeat "$REPEAT" \
    --file "$TEST_FILE" --range 0-0 --direct on --type sequence --seed "$SEED"

  run_case "write_seq_on" \
    --rw write --block_size "$BS" --block_count "$BC_WRITE_SEQ_ON" --repeat "$REPEAT" \
    --file "$TEST_FILE" --range 0-"$HI_ON" --direct on --type sequence --seed "$SEED"

  echo "Все результаты: $OUT_ROOT"
}

main "$@"
