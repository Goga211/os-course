#!/usr/bin/env bash
set -euo pipefail

CPU_BIN=./cpu-factorize

CMD=("$CPU_BIN" "$@")
CORES=$(nproc)

echo "Параметры cpu-factorize:"
printf '  %q ' "${CMD[@]}"
echo
echo "Логических ядер: $CORES"


#ОДИН ПРОЦЕСС
echo "один процесс (baseline)"
echo "--- 1A) /usr/bin/time -v для одного процесса ---"
/usr/bin/time -v "${CMD[@]}" 2> time_single.txt

echo
grep -E 'User time|System time|Elapsed|Maximum resident' time_single.txt || true

echo
echo "--- 1B) pidstat для одного процесса ---"
"${CMD[@]}" &    # запускаем отдельно для pidstat
PID=$!

pidstat -w -p "$PID" 1 > pidstat_single.txt &

wait "$PID"

echo
tail -n 15 pidstat_single.txt

#ФУНКЦИЯ ДЛЯ N+ ПРОЦЕССОВ
run_multi() {
  local factor="$1"   # 1, 2, 4
  local label="$2"    # N, 2N, 4N

  local PROCS=$((CORES * factor))
  echo
  echo "$label: $PROCS процессов (factor=$factor)"

  local PIDS=()

  echo "Запускаем $PROCS процессов cpu-factorize..."
  local start_ts
  start_ts=$(date +%s)

  for i in $(seq 1 "$PROCS"); do
    "${CMD[@]}" &
    PIDS+=($!)
  done

  echo "Мониторим контекстные переключения через pidstat в pidstat_${label}.txt"
  pidstat -w 1 -p "$(printf ",%s" "${PIDS[@]}")" > "pidstat_${label}.txt" &

  # ждём завершения всех процессов
  for PID in "${PIDS[@]}"; do
    wait "$PID"
  done

  local end_ts
  end_ts=$(date +%s)
  local elapsed=$((end_ts - start_ts))

  echo
  echo "Эксперимент $label завершён. Примерное реальное время: ${elapsed} секунд."
  echo "Последние строки pidstat_${label}.txt:"
  tail -n 15 "pidstat_${label}.txt"
}

# ЭКСПЕРИМЕНТ 2: N ПРОЦЕССОВ
run_multi 1 "N"

# ЭКСПЕРИМЕНТ 3: 2N ПРОЦЕССОВ
run_multi 2 "2N"

# ЭКСПЕРИМЕНТ 4: 4N ПРОЦЕССОВ
run_multi 4 "4N"
