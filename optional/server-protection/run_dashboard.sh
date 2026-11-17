#!/usr/bin/env bash
# Production dashboard + API — requires CSF. No demo mode.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

if ! command -v csf >/dev/null 2>&1; then
  echo "FATAL: CSF not found. Install ConfigServer Security & Firewall first."
  echo "This build does not include demo/mock mode."
  exit 1
fi

export SPP_HOST="${SPP_HOST:-127.0.0.1}"
export SPP_PORT="${SPP_PORT:-8080}"
export SPP_REQUIRE_ROOT="${SPP_REQUIRE_ROOT:-1}"

echo "Server Protection Pro (production)"
echo "  URL:  http://${SPP_HOST}:${SPP_PORT}/"
echo "  CSF:  $(command -v csf)"
echo "  User: $(id -un) (uid=$(id -u))"
echo "  Mutations require root: ${SPP_REQUIRE_ROOT}"
echo ""
exec python3 "$DIR/server_protection_api.py"
