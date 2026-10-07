#!/usr/bin/env bash
set -euo pipefail

IO_OS="./io-load"
IO_VTPC="./io-load-vtpc"
VTPC_LIB_DIR="../../vtpc/build/lib"

FILE="/var/tmp/vtpc_bench.bin"
NPROCS=4

CACHE_BYTES=$((32 * 1024 * 1024))   # 32 MiB cache (фиксировано)

FILE_SIZES_MB=(32 64 128 256 512 1024 2048)
DIRECTS=(off on)
BLOCK_SIZES=(4096 65536 1048576)
TYPES=(random sequence)

REPEAT=2
SEED=123456
ALIGN=4096

LOG_DIR="logs"
SUMMARY="summary.csv"
mkdir -p "$LOG_DIR"
: > "$SUMMARY"

echo "backend,direct,type,block_size,file_size_mb,cache_to_file_ratio,block_count,range_per_proc,file_size_bytes,wall_ms,bw_mib_s,iops,hits,misses,hitrate_pct" >> "$SUMMARY"

parse() {
  echo "$1" | awk '
  BEGIN{bw=-1;iops=-1;h=0;m=0}
  {
    if(match($0,/BW=([0-9.]+)/,a))bw=a[1];
    if(match($0,/IOPS=([0-9.]+)/,a))iops=a[1];
    if(match($0,/hits=([0-9]+)/,a))h=a[1];
    if(match($0,/misses=([0-9]+)/,a))m=a[1];
  }
  END{printf "%.3f,%.0f,%s,%s\n",bw,iops,h,m}'
}

now() { date +%s%N; }

for backend in os vtpc; do
  if [[ "$backend" == "vtpc" ]]; then
    export LD_LIBRARY_PATH="$VTPC_LIB_DIR:${LD_LIBRARY_PATH:-}"
  fi

  for TYPE in "${TYPES[@]}"; do
    for direct in "${DIRECTS[@]}"; do
      for bs in "${BLOCK_SIZES[@]}"; do
        for fmb in "${FILE_SIZES_MB[@]}"; do

          fsz=$(( fmb * 1024 * 1024 ))
          # делаем файл кратным NPROCS*ALIGN
          chunk=$(( (fsz / NPROCS / ALIGN) * ALIGN ))
          if (( chunk < ALIGN )); then chunk=$ALIGN; fi
          rpp=$chunk
          fsz=$(( rpp * NPROCS ))

          # сколько блоков в диапазоне процесса
          bc=$(( rpp / bs ))
          if (( bc < 1 )); then bc=1; rpp=$(( bs )); fsz=$(( rpp * NPROCS )); fi

          ratio=$(python3 - <<PY
cache=$CACHE_BYTES
file=$fsz
print(f"{cache/file:.6f}")
PY
)

          truncate -s "$fsz" "$FILE"

          echo "==> backend=$backend type=$TYPE direct=$direct bs=$bs file=${fmb}MiB cache/file=$ratio"

          t0=$(now)

          for i in $(seq 1 $NPROCS); do
            lo=$(( (i-1)*rpp )); hi=$(( lo+rpp ))
            bin="$IO_OS"; [[ "$backend" == "vtpc" ]] && bin="$IO_VTPC"

            ($bin \
              --rw read --block_size "$bs" --block_count "$bc" \
              --file "$FILE" --range "$lo-$hi" --direct "$direct" \
              --type "$TYPE" --repeat "$REPEAT" --seed "$SEED" \
              2> "$LOG_DIR/${backend}_t${TYPE}_d${direct}_bs${bs}_f${fmb}.p$i.log") &
          done
          wait

          t1=$(now)
          wall_ms=$(( (t1 - t0)/1000000 ))

          sum_bw=0; sum_iops=0; sum_hits=0; sum_miss=0
          for i in $(seq 1 $NPROCS); do
            line=$(grep '\[io-load\]' "$LOG_DIR/${backend}_t${TYPE}_d${direct}_bs${bs}_f${fmb}.p$i.log" | tail -1)
            p=$(parse "$line")
            bw=$(echo "$p" | cut -d, -f1)
            iops=$(echo "$p" | cut -d, -f2)
            h=$(echo "$p" | cut -d, -f3)
            m=$(echo "$p" | cut -d, -f4)

            sum_bw=$(awk "BEGIN{print $sum_bw+$bw}")
            sum_iops=$(awk "BEGIN{print $sum_iops+$iops}")
            sum_hits=$((sum_hits+h))
            sum_miss=$((sum_miss+m))
          done

          hr=$(awk "BEGIN{print ($sum_hits+$sum_miss==0)?0:(100*$sum_hits/($sum_hits+$sum_miss))}")

          echo "$backend,$direct,$TYPE,$bs,$fmb,$ratio,$bc,$rpp,$fsz,$wall_ms,$sum_bw,$sum_iops,$sum_hits,$sum_miss,$hr" >> "$SUMMARY"
          echo "   BW=$sum_bw MiB/s HR=$hr% wall=${wall_ms}ms"

        done
      done
    done
  done
done

echo "DONE → $SUMMARY"
