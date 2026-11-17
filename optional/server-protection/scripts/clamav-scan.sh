#!/usr/bin/env bash
set -euo pipefail
LOG="${SPP_LOG_DIR:-/var/log/server-protection}/clamav-scan.log"
mkdir -p "$(dirname "$LOG")"
{
  echo "=== ClamAV scan $(date -Iseconds) ==="
  if command -v clamdscan >/dev/null 2>&1; then
    clamdscan --fdpass /tmp /home 2>&1 || true
  elif command -v clamscan >/dev/null 2>&1; then
    clamscan -r --infected --max-filesize=50M /tmp 2>&1 || true
  else
    echo "clamav not installed"
    exit 1
  fi
} | tee -a "$LOG"
