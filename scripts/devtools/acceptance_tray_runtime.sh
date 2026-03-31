#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
usage: acceptance_tray_runtime.sh [--build-dir DIR] [--log-dir DIR] [--live-wayland]

运行 tray / tray_menu 验收。

默认行为：
- 运行 tray 相关 Meson 测试目标
- 记录统一日志到 BIT_SHELL_LOG_DIR 或 ~/logs

可选 live smoke：
- 需要 WAYLAND_DISPLAY、NIRI_SOCKET、niri 命令与已编译的 bit_shelld / bit_bar
- 仅验证 bit_bar layer surface 挂载与退出清理，不代替人工 popup 交互检查

环境变量：
- BIT_SHELL_BUILD_DIR: build 目录，默认 <repo>/build
- BIT_SHELL_LOG_DIR: 日志目录，默认 ~/logs
EOF
}

step() {
  printf '\n==> %s\n' "$*"
}

require_file() {
  local path="$1"
  if [[ ! -e "$path" ]]; then
    printf 'missing required file: %s\n' "$path" >&2
    exit 1
  fi
}

run_cmd() {
  step "$*"
  "$@"
}

resolve_log_dir() {
  local preferred="$1"
  local fallback="${repo_root}/build/devtools-logs"

  if mkdir -p "$preferred" 2>/dev/null && [[ -w "$preferred" ]]; then
    printf '%s\n' "$preferred"
    return
  fi

  mkdir -p "$fallback"
  printf '%s\n' "$fallback"
}

wait_for_grep() {
  local command="$1"
  local pattern="$2"
  local attempts="${3:-20}"
  local delay="${4:-0.2}"
  local output

  for ((i = 0; i < attempts; i++)); do
    output="$(eval "$command" 2>/dev/null || true)"
    if printf '%s' "$output" | grep -Eq "$pattern"; then
      return 0
    fi
    sleep "$delay"
  done
  return 1
}

namespace_present() {
  local namespace="$1"

  [[ -n "${NIRI_SOCKET:-}" ]] \
    && command -v niri >/dev/null 2>&1 \
    && NIRI_SOCKET="${NIRI_SOCKET}" niri msg --json layers 2>/dev/null \
         | grep -Eq "\"namespace\"[[:space:]]*:[[:space:]]*\"${namespace}\""
}

require_clean_process_name() {
  local process_name="$1"
  local pids

  pids="$(pgrep -x "$process_name" || true)"
  if [[ -n "$pids" ]]; then
    printf 'live smoke requires no running %s instance; found pid(s): %s\n' "$process_name" "$pids" >&2
    exit 1
  fi
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
build_dir="${BIT_SHELL_BUILD_DIR:-${repo_root}/build}"
log_dir="${BIT_SHELL_LOG_DIR:-${HOME}/logs}"
live_wayland=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --log-dir)
      log_dir="$2"
      shift 2
      ;;
    --live-wayland)
      live_wayland=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'unknown argument: %s\n' "$1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

log_dir="$(resolve_log_dir "$log_dir")"
log_file="${log_dir}/acceptance_tray_runtime_$(date +%Y-%m-%d_%H-%M-%S).log"
exec > >(tee -a "$log_file") 2>&1

step "tray runtime acceptance log: ${log_file}"
require_file "${repo_root}/meson.build"
require_file "${build_dir}/build.ninja"

run_cmd meson compile -C "$build_dir"
run_cmd meson test -C "$build_dir" \
  bit_bar_tray_menu_roundtrip \
  tray_utf8_ingress \
  bit_bar_tray_text_utf8 \
  bit_bar_tray_menu_ux

if [[ "$live_wayland" -eq 0 ]]; then
  step "skip live Wayland smoke (pass --live-wayland to enable)"
  exit 0
fi

require_file "${build_dir}/core/bit_shelld"
require_file "${build_dir}/core/bit_bar"

if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${NIRI_SOCKET:-}" ]]; then
  printf 'live smoke requires WAYLAND_DISPLAY and NIRI_SOCKET\n' >&2
  exit 1
fi
if ! command -v niri >/dev/null 2>&1; then
  printf 'live smoke requires niri command in PATH\n' >&2
  exit 1
fi
require_clean_process_name bit_bar

runtime_dir="${XDG_RUNTIME_DIR:-/tmp}"
socket_path="$(mktemp -u "${runtime_dir}/bit_shell_acceptance_bar.XXXXXX.sock")"
shelld_pid=""
bar_pid=""
layer_check_enabled=1

cleanup() {
  if [[ -n "$bar_pid" ]] && kill -0 "$bar_pid" 2>/dev/null; then
    kill "$bar_pid" 2>/dev/null || true
    wait "$bar_pid" 2>/dev/null || true
  fi
  if [[ -n "$shelld_pid" ]] && kill -0 "$shelld_pid" 2>/dev/null; then
    kill "$shelld_pid" 2>/dev/null || true
    wait "$shelld_pid" 2>/dev/null || true
  fi
  rm -f "$socket_path"
}
trap cleanup EXIT

step "start bit_shelld on ${socket_path}"
BIT_SHELL_SOCKET="$socket_path" "${build_dir}/core/bit_shelld" &
shelld_pid="$!"

for ((i = 0; i < 30; i++)); do
  if [[ -S "$socket_path" ]]; then
    break
  fi
  sleep 0.2
done
if [[ ! -S "$socket_path" ]]; then
  printf 'bit_shelld did not create IPC socket: %s\n' "$socket_path" >&2
  exit 1
fi

step "start bit_bar live smoke"
G_DEBUG=fatal-criticals BIT_SHELL_SOCKET="$socket_path" "${build_dir}/core/bit_bar" &
bar_pid="$!"

if namespace_present "bit-shell-bar"; then
  step "existing bit-shell-bar surface detected; skip namespace-based layer checks"
  layer_check_enabled=0
fi

if [[ "$layer_check_enabled" -eq 1 ]]; then
  if ! wait_for_grep \
    "NIRI_SOCKET='${NIRI_SOCKET}' niri msg --json layers" \
    '"namespace"[[:space:]]*:[[:space:]]*"bit-shell-bar"'; then
    printf 'bit_bar layer surface was not observed in niri layers\n' >&2
    exit 1
  fi

  step "bit_bar layer surface observed"
fi

kill "$bar_pid"
wait "$bar_pid" || true
bar_pid=""

if [[ "$layer_check_enabled" -eq 1 ]]; then
  if wait_for_grep \
    "NIRI_SOCKET='${NIRI_SOCKET}' niri msg --json layers" \
    '"namespace"[[:space:]]*:[[:space:]]*"bit-shell-bar"'; then
    printf 'bit_bar layer surface still present after exit\n' >&2
    exit 1
  fi

  step "bit_bar layer surface cleaned up after exit"
fi
