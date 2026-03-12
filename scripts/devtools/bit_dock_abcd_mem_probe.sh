#!/usr/bin/env bash
set -euo pipefail

# ====== 你先改这里 ======
BIN="${BIN:-/home/cestlavie/bit_shell/build-heap/core/bit_dock}"
# ========================

OUT_DIR="${OUT_DIR:-./dock_abcd_probe_$(date +%Y%m%d_%H%M%S)}"
WAIT_SECS="${WAIT_SECS:-20}"

mkdir -p "$OUT_DIR"

log() {
  printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing command: $1" >&2
    exit 1
  }
}

pid_of_app() {
  pidof "$(basename "$BIN")" 2>/dev/null || true
}

kill_app() {
  pkill -x "$(basename "$BIN")" 2>/dev/null || true
  sleep 2
}

wait_for_pid() {
  local i pid
  for i in $(seq 1 30); do
    pid="$(pid_of_app)"
    if [[ -n "$pid" ]]; then
      echo "$pid"
      return 0
    fi
    sleep 1
  done
  return 1
}

smaps_rollup_brief() {
  local pid="$1"
  egrep '^(Rss|Pss|Private_Clean|Private_Dirty|Anonymous|AnonHugePages|Pss_Anon|Pss_File):' "/proc/$pid/smaps_rollup" || true
}

capture_smaps_brief() {
  local label="$1"
  local pid
  pid="$(pid_of_app)"
  if [[ -z "$pid" ]]; then
    echo "bit_dock not running" | tee "$OUT_DIR/${label}.txt"
    return
  fi

  {
    echo "label=$label"
    echo "pid=$pid"
    echo "timestamp=$(date '+%F %T')"
    echo
    smaps_rollup_brief "$pid"
  } | tee "$OUT_DIR/${label}.txt"
}

run_once_with_env() {
  local label="$1"
  shift

  kill_app
  log "starting [$label]"

  "$@" "$BIN" >"$OUT_DIR/${label}.stdout.log" 2>"$OUT_DIR/${label}.stderr.log" &
  local spawned=$!
  echo "$spawned" > "$OUT_DIR/${label}.spawned_pid.txt"

  sleep "$WAIT_SECS"

  capture_smaps_brief "$label"

  if pid="$(pid_of_app)"; then
    {
      echo
      echo "=== pmap top20 by RSS ==="
      pmap -x "$pid" | sort -k3 -n | tail -20
    } >> "$OUT_DIR/${label}.txt" 2>&1 || true
  fi

  kill_app
}

# ---------- A 组：renderer 对照 ----------
group_A() {
  need_cmd pmap

  log "running group A (renderer matrix)"

  run_once_with_env "A_default" env
  run_once_with_env "A_cairo" env GSK_RENDERER=cairo
  run_once_with_env "A_gl"    env GSK_RENDERER=gl
  run_once_with_env "A_ngl"   env GSK_RENDERER=ngl

  # vulkan 可能失败，失败也保留日志
  run_once_with_env "A_vulkan" env GSK_RENDERER=vulkan || true

  log "group A done -> $OUT_DIR/A_*"
}

# ---------- B 组：cairo + heaptrack ----------
group_B() {
  need_cmd heaptrack
  need_cmd heaptrack_print

  log "running group B (heaptrack under cairo)"
  kill_app

  local old_latest new_latest
  old_latest="$(ls -t heaptrack*.zst heaptrack*.gz 2>/dev/null | head -1 || true)"

  GSK_RENDERER=cairo heaptrack "$BIN"
  sleep 1

  new_latest="$(ls -t heaptrack*.zst heaptrack*.gz 2>/dev/null | head -1 || true)"
  if [[ -z "$new_latest" ]]; then
    echo "heaptrack output not found" >&2
    exit 1
  fi

  echo "$new_latest" | tee "$OUT_DIR/B_heaptrack_latest.txt"

  heaptrack_print "$new_latest" > "$OUT_DIR/B_heaptrack_report.txt" 2>"$OUT_DIR/B_heaptrack_print.stderr.log" || true

  {
    echo "=== first 220 lines ==="
    sed -n '1,220p' "$OUT_DIR/B_heaptrack_report.txt"
    echo
    echo "=== grep peak/leak/allocation ==="
    grep -nEi 'PEAK MEMORY CONSUMERS|MOST TEMPORARY ALLOCATIONS|LEAK|leak|peak memory consumed|MOST CALLS TO ALLOCATION FUNCTIONS' \
      "$OUT_DIR/B_heaptrack_report.txt" | head -120 || true
  } > "$OUT_DIR/B_heaptrack_summary.txt"

  log "group B done -> $OUT_DIR/B_*"
}

# ---------- C 组：MALLOC_ARENA_MAX 对照 ----------
group_C() {
  need_cmd pmap

  log "running group C (MALLOC_ARENA_MAX comparison)"

  run_once_with_env "C_default" env
  run_once_with_env "C_arena1_default" env MALLOC_ARENA_MAX=1
  run_once_with_env "C_arena1_cairo" env GSK_RENDERER=cairo MALLOC_ARENA_MAX=1

  log "group C done -> $OUT_DIR/C_*"
}

# ---------- D 组：valgrind leak check under cairo ----------
group_D() {
  need_cmd valgrind

  log "running group D (valgrind memcheck under cairo)"
  kill_app

  GSK_RENDERER=cairo \
  valgrind \
    --tool=memcheck \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --num-callers=25 \
    --log-file="$OUT_DIR/D_valgrind.log" \
    "$BIN"

  {
    echo "=== leak summary ==="
    grep -nE 'definitely lost|indirectly lost|possibly lost|still reachable|ERROR SUMMARY' \
      "$OUT_DIR/D_valgrind.log" | head -50 || true
    echo
    echo "=== first definitely lost hits ==="
    grep -n 'definitely lost' "$OUT_DIR/D_valgrind.log" | head -10 || true
  } > "$OUT_DIR/D_valgrind_summary.txt"

  log "group D done -> $OUT_DIR/D_*"
}

group_ALL() {
  group_A
  group_B
  group_C
  group_D
}

usage() {
  cat <<EOF
用法:
  BIN=/home/cestlavie/bit_shell/build-heap/core/bit_dock ./dock_abcd_probe.sh A
  BIN=/home/cestlavie/bit_shell/build-heap/core/bit_dock ./dock_abcd_probe.sh B
  BIN=/home/cestlavie/bit_shell/build-heap/core/bit_dock ./dock_abcd_probe.sh C
  BIN=/home/cestlavie/bit_shell/build-heap/core/bit_dock ./dock_abcd_probe.sh D
  BIN=/home/cestlavie/bit_shell/build-heap/core/bit_dock ./dock_abcd_probe.sh ALL

环境变量:
  BIN        bit_dock 完整路径
  OUT_DIR    输出目录
  WAIT_SECS  每次启动后等待秒数，默认 20

输出文件:
  A_*.txt / A_*.stdout.log / A_*.stderr.log
  B_heaptrack_*.txt
  C_*.txt
  D_valgrind*.txt
EOF
}

main() {
  if [[ ! -x "$BIN" ]]; then
    echo "binary not found or not executable: $BIN" >&2
    exit 1
  fi

  local mode="${1:-ALL}"

  case "$mode" in
    A|a)   group_A ;;
    B|b)   group_B ;;
    C|c)   group_C ;;
    D|d)   group_D ;;
    ALL|all) group_ALL ;;
    *)
      usage
      exit 1
      ;;
  esac

  log "done. output dir: $OUT_DIR"
}

main "$@"
