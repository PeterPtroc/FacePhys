#!/usr/bin/env bash
set -euo pipefail

native_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export LD_LIBRARY_PATH="$native_dir/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$native_dir/bin/facephys_native" "${1:-$native_dir/config/nanopi_neo_st7735.json}"
