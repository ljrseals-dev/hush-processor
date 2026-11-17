#!/usr/bin/env bash
set -euo pipefail
LOG="${SPP_LOG_DIR:-/var/log/server-protection}/aide.log"
mkdir -p "$(dirname "$LOG")"
{
  echo "=== AIDE check $(date -Iseconds) ==="
  if command -v aide >/dev/null 2>&1; then
    aide --check 2>&1 || true
  else
    echo "aide not installed"
    exit 1
  fi
} | tee -a "$LOG"
