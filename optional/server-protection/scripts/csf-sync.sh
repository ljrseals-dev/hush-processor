#!/usr/bin/env bash
# Lightweight CSF helper used by the API / cron
set -euo pipefail
case "${1:-reload}" in
  reload) csf -r ;;
  restart) csf -ra ;;
  status) csf -s ;;
  *) echo "usage: $0 reload|restart|status"; exit 1 ;;
esac
