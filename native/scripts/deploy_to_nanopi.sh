#!/usr/bin/env bash
set -euo pipefail

native_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$native_dir/.." && pwd)"
app_build="${1:-$repo_dir/build/native-armhf}"
target="${TARGET:-pi@10.0.0.243}"
target_root="${TARGET_ROOT:-/home/pi/FacePhys-Demo}"
ssh_command=(ssh)
rsync_rsh="ssh"
if [[ "${USE_SSHPASS:-0}" == "1" ]]; then
  command -v sshpass >/dev/null || { echo "USE_SSHPASS=1 requires sshpass on the build host" >&2; exit 1; }
  : "${SSHPASS:?Set SSHPASS when USE_SSHPASS=1 (or use SSH keys instead)}"
  ssh_command=(sshpass -e ssh)
  rsync_rsh="sshpass -e ssh"
fi

test -x "$app_build/facephys_native" || { echo "Missing $app_build/facephys_native; run build_armhf.sh first" >&2; exit 1; }
"${ssh_command[@]}" "$target" "mkdir -p '$target_root/native/bin'"
rsync -e "$rsync_rsh" -av --exclude build --exclude '*.o' "$native_dir/" "$target:$target_root/native/"
rsync -e "$rsync_rsh" -av --include='*.tflite' --include='state.gz' --exclude='*' "$repo_dir/models/" "$target:$target_root/models/"
rsync -e "$rsync_rsh" -av "$app_build/facephys_native" "$app_build/inspect_tflite_models" "$app_build/run_offline_test" "$app_build/test_camera" "$app_build/test_tft" "$target:$target_root/native/bin/"
test -d "$app_build/runtime" || { echo "Missing $app_build/runtime; rebuild with build_armhf.sh" >&2; exit 1; }
rsync -e "$rsync_rsh" -av "$app_build/runtime/" "$target:$target_root/native/bin/"
echo "Deployed to $target:$target_root/native"
