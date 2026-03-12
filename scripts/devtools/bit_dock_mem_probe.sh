#!/usr/bin/env bash
set -euo pipefail

APP_NAME="${APP_NAME:-bit_dock}"
OUT_DIR="${OUT_DIR:-./bit_dock_mem_probe_$(date +%Y%m%d_%H%M%S)}"
SAMPLE_INTERVAL="${SAMPLE_INTERVAL:-10}"
IDLE_DURATION="${IDLE_DURATION:-60}"
POST_HOVER_DURATION="${POST_HOVER_DURATION:-60}"

mkdir -p "$OUT_DIR"

log() {
  printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

get_pid() {
  pidof "$APP_NAME" 2>/dev/null || true
}

wait_for_pid() {
  local pid=""
  local i
  for i in $(seq 1 30); do
    pid="$(get_pid)"
    if [[ -n "$pid" ]]; then
      echo "$pid"
      return 0
    fi
    sleep 1
  done
  return 1
}

collect_basic_env() {
  {
    echo "==== basic env ===="
    date
    uname -a
    echo
    echo "APP_NAME=$APP_NAME"
    echo "OUT_DIR=$OUT_DIR"
    echo "GSK_RENDERER=${GSK_RENDERER:-}"
    echo "LIBGL_ALWAYS_SOFTWARE=${LIBGL_ALWAYS_SOFTWARE:-}"
    echo "GALLIUM_DRIVER=${GALLIUM_DRIVER:-}"
    echo
    echo "==== os-release ===="
    cat /etc/os-release 2>/dev/null || true
  } > "$OUT_DIR/basic_env.txt"

  if have_cmd glxinfo; then
    glxinfo -B > "$OUT_DIR/glxinfo_B.txt" 2>&1 || true
  else
    echo "glxinfo not found" > "$OUT_DIR/glxinfo_B.txt"
  fi
}

sample_smaps_rollup() {
  local pid="$1"
  local label="$2"
  local seconds="$3"
  local outfile="$OUT_DIR/${label}_smaps_rollup_samples.txt"
  local end_ts
  end_ts=$(( $(date +%s) + seconds ))

  {
    echo "label=$label"
    echo "pid=$pid"
    echo "seconds=$seconds"
    echo
  } > "$outfile"

  while (( $(date +%s) < end_ts )); do
    if [[ ! -r "/proc/$pid/smaps_rollup" ]]; then
      echo "process disappeared" >> "$outfile"
      break
    fi

    {
      echo "==== $(date '+%F %T') ===="
      egrep '^(Rss|Pss|Private_Clean|Private_Dirty|Anonymous|AnonHugePages|Pss_Anon|Pss_File|Pss_Shmem):' "/proc/$pid/smaps_rollup" || true
      echo
    } >> "$outfile"

    sleep "$SAMPLE_INTERVAL"
  done
}

capture_pmap() {
  local pid="$1"
  if have_cmd pmap; then
    pmap -x "$pid" > "$OUT_DIR/pmap_full.txt" 2>&1 || true
    pmap -x "$pid" | sort -k3 -n | tail -30 > "$OUT_DIR/pmap_top30_by_rss.txt" 2>&1 || true
  else
    echo "pmap not found" > "$OUT_DIR/pmap_full.txt"
  fi
}

capture_smaps_anon_breakdown() {
  local pid="$1"
  local outfile="$OUT_DIR/smaps_anon_breakdown.txt"

  awk '
  /^[0-9a-f]/ {
    hdr=$0
    is_anon = (index($0, "[ anon ]") || index($0, "[heap]"))
    rss=pss=pc=pd=anon=ahp=0
  }
  /^Rss:/ { rss=$2 }
  /^Pss:/ { pss=$2 }
  /^Private_Clean:/ { pc=$2 }
  /^Private_Dirty:/ { pd=$2 }
  /^Anonymous:/ { anon=$2 }
  /^AnonHugePages:/ {
    ahp=$2
    if (is_anon) {
      printf "%8d %8d %8d %8d %8d %8d  %s\n", rss, pss, pc, pd, anon, ahp, hdr
    }
  }' "/proc/$pid/smaps" | sort -n > "$outfile"
}

capture_maps_and_status() {
  local pid="$1"
  cat "/proc/$pid/status" > "$OUT_DIR/proc_status.txt" 2>&1 || true
  cat "/proc/$pid/maps" > "$OUT_DIR/proc_maps.txt" 2>&1 || true
  cat "/proc/$pid/smaps_rollup" > "$OUT_DIR/proc_smaps_rollup_final.txt" 2>&1 || true
}

renderer_probe_once() {
  local renderer="$1"
  local subdir="$OUT_DIR/renderer_${renderer}"
  mkdir -p "$subdir"

  log "probing renderer=$renderer"
  pkill -x "$APP_NAME" 2>/dev/null || true
  sleep 2

  (
    export GSK_RENDERER="$renderer"
    "$APP_NAME" > "$subdir/stdout.log" 2> "$subdir/stderr.log" &
    echo $! > "$subdir/spawned_pid.txt"
  )

  sleep 20
  local pid
  pid="$(get_pid)"

  {
    echo "renderer=$renderer"
    echo "pid=$pid"
    echo "timestamp=$(date '+%F %T')"
    echo
    if [[ -n "$pid" && -r "/proc/$pid/smaps_rollup" ]]; then
      egrep '^(Rss|Pss|Private_Clean|Private_Dirty|Anonymous|AnonHugePages|Pss_Anon|Pss_File|Pss_Shmem):' "/proc/$pid/smaps_rollup" || true
      echo
      if have_cmd pmap; then
        pmap -x "$pid" | sort -k3 -n | tail -20 || true
      fi
    else
      echo "bit_dock not running or smaps_rollup unavailable"
    fi
  } > "$subdir/summary.txt"

  pkill -x "$APP_NAME" 2>/dev/null || true
  sleep 2
}

run_renderer_matrix() {
  log "running renderer matrix"
  renderer_probe_once gl
  renderer_probe_once ngl
  renderer_probe_once cairo
  renderer_probe_once vulkan
}

main_live_probe() {
  local pid
  pid="$(get_pid)"
  if [[ -z "$pid" ]]; then
    log "$APP_NAME is not running. Please start it first."
    exit 1
  fi

  log "attached to pid=$pid"
  capture_maps_and_status "$pid"
  capture_pmap "$pid"
  capture_smaps_anon_breakdown "$pid"

  log "phase 1: idle sampling for ${IDLE_DURATION}s"
  sample_smaps_rollup "$pid" "phase1_idle" "$IDLE_DURATION"

  echo
  echo "现在请在 ${APP_NAME} 上持续移动鼠标、触发 hover / magnification / 打开应用等你怀疑会涨内存的操作。"
  echo "按 Enter 开始记录这一阶段。"
  read -r

  pid="$(get_pid)"
  if [[ -n "$pid" ]]; then
    log "phase 2: active interaction sampling for ${IDLE_DURATION}s"
    sample_smaps_rollup "$pid" "phase2_interaction" "$IDLE_DURATION"
  fi

  echo
  echo "现在停止操作，让程序空闲。按 Enter 开始记录回落阶段。"
  read -r

  pid="$(get_pid)"
  if [[ -n "$pid" ]]; then
    log "phase 3: post-hover idle sampling for ${POST_HOVER_DURATION}s"
    sample_smaps_rollup "$pid" "phase3_post_idle" "$POST_HOVER_DURATION"
    capture_maps_and_status "$pid"
    capture_pmap "$pid"
    capture_smaps_anon_breakdown "$pid"
  fi
}

write_summary_readme() {
  cat > "$OUT_DIR/README.txt" <<'EOF'
文件说明：

basic_env.txt
  基础环境信息

glxinfo_B.txt
  OpenGL / 渲染器信息

phase1_idle_smaps_rollup_samples.txt
phase2_interaction_smaps_rollup_samples.txt
phase3_post_idle_smaps_rollup_samples.txt
  三阶段内存采样

pmap_full.txt
pmap_top30_by_rss.txt
  进程映射明细和大块映射排行

smaps_anon_breakdown.txt
  匿名段 / heap 大块明细

renderer_gl/
renderer_ngl/
renderer_cairo/
renderer_vulkan/
  四种 GSK_RENDERER 对照结果

建议回传这些文件内容：
1. glxinfo_B.txt
2. phase1/2/3 三个 smaps_rollup 采样文件
3. pmap_top30_by_rss.txt
4. smaps_anon_breakdown.txt
5. 各 renderer_*/summary.txt
EOF
}

usage() {
  cat <<EOF
用法:
  $0 live        # 对当前正在运行的 bit_dock 做 3 阶段采样
  $0 renderers   # 轮流用 gl/ngl/cairo/vulkan 启动 bit_dock 并采样
  $0 all         # 先采样 live，再做 renderers
  $0 help

可选环境变量:
  APP_NAME=bit_dock
  OUT_DIR=./bit_dock_mem_probe_xxx
  SAMPLE_INTERVAL=10
  IDLE_DURATION=60
  POST_HOVER_DURATION=60
EOF
}

cmd="${1:-all}"

collect_basic_env
write_summary_readme

case "$cmd" in
  live)
    main_live_probe
    ;;
  renderers)
    run_renderer_matrix
    ;;
  all)
    main_live_probe
    run_renderer_matrix
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    usage
    exit 1
    ;;
esac

log "done. report directory: $OUT_DIR"
