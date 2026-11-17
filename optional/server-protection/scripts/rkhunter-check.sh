#!/usr/bin/env bash
set -euo pipefail
LOG="${SPP_LOG_DIR:-/var/log/server-protection}/rkhunter.log"
mkdir -p "$(dirname "$LOG")"
{
  echo "=== rkhunter $(date -Iseconds) ==="
  if command -v rkhunter >/dev/null 2>&1; then
    rkhunter --check --sk --nocolors 2>&1 || true
  else
    echo "rkhunter not installed"
    exit 1
  fi
} | tee -a "$LOG"
