#!/usr/bin/env bash
# Thin wrapper — see msc_host_test.py
# Usage: sudo ./msc_host_test.sh [/dev/sda] [http://ESP] [fav1]
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
DEV="${1:-/dev/sda}"
ESP="${2:-http://192.168.178.89}"
UID="${3:-fav1}"
exec python3 "$DIR/msc_host_test.py" --dev "$DEV" --esp "$ESP" --uid "$UID"
