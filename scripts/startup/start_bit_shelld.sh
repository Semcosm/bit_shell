#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
binary_path="${repo_root}/build/core/bit_shelld"

if [[ ! -x "${binary_path}" ]]; then
  echo "bit_shelld binary not found: ${binary_path}" >&2
  echo "run: meson compile -C build" >&2
  exit 1
fi

exec "${binary_path}" "$@"
