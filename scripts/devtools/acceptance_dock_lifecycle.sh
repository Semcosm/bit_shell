#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
usage: acceptance_dock_lifecycle.sh [--build-dir DIR] [--log-dir DIR] [--live-wayland]

运行 dock lifecycle 验收。

默认行为：
- 运行 dock lifecycle Meson 测试目标
- 记录统一日志到 BIT_SHELL_LOG_DIR 或 ~/logs

可选 live smoke：
- 需要 WAYLAND_DISPLAY、已编译 bit_dock，以及 python3
- 若同时提供 NIRI_SOCKET 和 niri 命令，会额外校验 bit-dock layer surface 挂载与退出清理

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

wait_for_file_grep() {
  local file="$1"
  local pattern="$2"
  local attempts="${3:-30}"
  local delay="${4:-0.2}"

  for ((i = 0; i < attempts; i++)); do
    if [[ -f "$file" ]] && grep -Eq "$pattern" "$file"; then
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
log_file="${log_dir}/acceptance_dock_lifecycle_$(date +%Y-%m-%d_%H-%M-%S).log"
exec > >(tee -a "$log_file") 2>&1

step "dock lifecycle acceptance log: ${log_file}"
require_file "${repo_root}/meson.build"
require_file "${build_dir}/build.ninja"

run_cmd meson compile -C "$build_dir"
run_cmd meson test -C "$build_dir" bit_dock_widget_lifecycle

if [[ "$live_wayland" -eq 0 ]]; then
  step "skip live Wayland smoke (pass --live-wayland to enable)"
  exit 0
fi

require_file "${build_dir}/core/bit_dock"
if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
  printf 'live smoke requires WAYLAND_DISPLAY\n' >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  printf 'live smoke requires python3\n' >&2
  exit 1
fi
require_clean_process_name bit_dock

runtime_dir="${XDG_RUNTIME_DIR:-/tmp}"
socket_path="$(mktemp -u "${runtime_dir}/bit_shell_acceptance_dock.XXXXXX.sock")"
server_pid=""
dock_pid=""
server_log="${log_dir}/acceptance_dock_lifecycle_fake_ipc_$(date +%Y-%m-%d_%H-%M-%S).log"
layer_check_enabled=1

cleanup() {
  if [[ -n "$dock_pid" ]] && kill -0 "$dock_pid" 2>/dev/null; then
    kill "$dock_pid" 2>/dev/null || true
    wait "$dock_pid" 2>/dev/null || true
  fi
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -f "$socket_path"
}
trap cleanup EXIT

step "start fake IPC server on ${socket_path}"
python3 - "$socket_path" >"$server_log" 2>&1 <<'PY' &
import json
import os
import socket
import sys
import time

sock_path = sys.argv[1]

messages = [
    {
        "kind": "snapshot",
        "state": {
            "dock": {
                "items": [
                    {
                        "app_key": "org.test.DockApp",
                        "desktop_id": "org.test.DockApp.desktop",
                        "name": "Dock App",
                        "icon_name": "folder",
                        "window_ids": ["w-1"],
                        "pinned": True,
                        "running": True,
                        "focused": True,
                        "pinned_index": 0,
                    }
                ]
            },
            "settings": {
                "dock": {
                    "icon_size_px": 48,
                    "magnification_enabled": False,
                    "hover_range_cap_units": 3,
                    "spacing_px": 8,
                    "bottom_margin_px": 8,
                    "show_running_indicator": True,
                    "animate_opening_apps": False,
                    "display_mode": "icons",
                    "center_on_primary_output": False,
                }
            },
        },
    },
    {
        "kind": "event",
        "topic": "dock",
        "payload": {
            "items": [
                {
                    "app_key": "org.test.DockApp",
                    "desktop_id": "org.test.DockApp.desktop",
                    "name": "Fallback",
                    "window_ids": ["w-1"],
                    "pinned": True,
                    "running": True,
                    "focused": True,
                    "pinned_index": 0,
                }
            ]
        },
    },
    {
        "kind": "event",
        "topic": "settings",
        "payload": {
            "dock": {
                "icon_size_px": 56,
                "magnification_enabled": False,
                "hover_range_cap_units": 3,
                "spacing_px": 10,
                "bottom_margin_px": 8,
                "show_running_indicator": True,
                "animate_opening_apps": False,
                "display_mode": "icons",
                "center_on_primary_output": False,
            }
        },
    },
    {
        "kind": "event",
        "topic": "dock",
        "payload": {
            "items": [
                {
                    "app_key": "org.test.DockApp",
                    "desktop_id": "org.test.DockApp.desktop",
                    "name": "Dock App",
                    "icon_name": "edit-copy",
                    "window_ids": ["w-1"],
                    "pinned": True,
                    "running": True,
                    "focused": True,
                    "pinned_index": 0,
                }
            ]
        },
    },
]

if os.path.exists(sock_path):
    os.unlink(sock_path)

server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(sock_path)
server.listen(1)
server.settimeout(5.0)

try:
    conn, _ = server.accept()
    conn.settimeout(0.1)
    start = time.time()
    while time.time() - start < 0.5:
        try:
            data = conn.recv(4096)
        except TimeoutError:
            data = b""
        if not data:
            time.sleep(0.05)
            continue
        if b"subscribe" in data:
            break
    for message in messages:
        conn.sendall((json.dumps(message) + "\n").encode("utf-8"))
        time.sleep(0.35)
    time.sleep(0.35)
    conn.close()
except socket.timeout:
    print("fake IPC server timed out waiting for client", file=sys.stderr)
    sys.exit(2)
finally:
    server.close()
    if os.path.exists(sock_path):
        os.unlink(sock_path)
PY
server_pid="$!"

for ((i = 0; i < 30; i++)); do
  if [[ -S "$socket_path" ]]; then
    break
  fi
  sleep 0.2
done
if [[ ! -S "$socket_path" ]]; then
  printf 'fake IPC server did not create socket: %s\n' "$socket_path" >&2
  exit 1
fi

step "start bit_dock live smoke"
G_DEBUG=fatal-criticals BIT_SHELL_SOCKET="$socket_path" "${build_dir}/core/bit_dock" &
dock_pid="$!"

if ! wait_for_file_grep \
  "$log_file" \
  '\[bit_dock\] IPC connected and subscribed to dock/settings topics'; then
  printf 'bit_dock did not complete IPC bootstrap, see %s\n' "$log_file" >&2
  exit 1
fi

if namespace_present "bit-dock"; then
  step "existing bit-dock surface detected; skip namespace-based layer checks"
  layer_check_enabled=0
fi

if [[ "$layer_check_enabled" -eq 1 && -n "${NIRI_SOCKET:-}" ]] && command -v niri >/dev/null 2>&1; then
  if ! wait_for_grep \
    "NIRI_SOCKET='${NIRI_SOCKET}' niri msg --json layers" \
    '"namespace"[[:space:]]*:[[:space:]]*"bit-dock"'; then
    printf 'bit_dock layer surface was not observed in niri layers\n' >&2
    exit 1
  fi
  step "bit_dock layer surface observed"
fi

for ((i = 0; i < 50; i++)); do
  if ! kill -0 "$server_pid" 2>/dev/null; then
    break
  fi
  sleep 0.2
done
if kill -0 "$server_pid" 2>/dev/null; then
  printf 'fake IPC server did not finish within timeout, see %s\n' "$server_log" >&2
  exit 1
fi

if ! wait "$server_pid"; then
  printf 'fake IPC server exited with failure, see %s\n' "$server_log" >&2
  exit 1
fi
server_pid=""

sleep 1

if ! kill -0 "$dock_pid" 2>/dev/null; then
  printf 'bit_dock exited unexpectedly during live smoke\n' >&2
  exit 1
fi

if grep -Eq "invalid unclassed pointer in cast to 'GtkImage'|GTK_IS_IMAGE \(image\) failed" "$log_file"; then
  printf 'detected forbidden GtkImage lifecycle critical in %s\n' "$log_file" >&2
  exit 1
fi

kill "$dock_pid"
wait "$dock_pid" || true
dock_pid=""

if [[ "$layer_check_enabled" -eq 1 && -n "${NIRI_SOCKET:-}" ]] && command -v niri >/dev/null 2>&1; then
  if wait_for_grep \
    "NIRI_SOCKET='${NIRI_SOCKET}' niri msg --json layers" \
    '"namespace"[[:space:]]*:[[:space:]]*"bit-dock"'; then
    printf 'bit_dock layer surface still present after exit\n' >&2
    exit 1
  fi
  step "bit_dock layer surface cleaned up after exit"
fi

step "fake IPC server log: ${server_log}"
