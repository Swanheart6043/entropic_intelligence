#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

sudo python3 "$SCRIPT_DIR/migrate_platform_urls.py" \
    --config /etc/patrol_robot_h1/config.ini
sudo cp "$INSTALL_DIR"/systemds/* /etc/systemd/system/
sudo systemctl daemon-reload
