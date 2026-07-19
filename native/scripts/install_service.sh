#!/usr/bin/env bash
set -euo pipefail

native_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sudo install -D -m 0644 "$native_dir/systemd/facephys.service" /etc/systemd/system/facephys.service
sudo systemctl daemon-reload
sudo systemctl enable facephys.service
echo "Installed. Start with: sudo systemctl start facephys.service"
