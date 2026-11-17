#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"

if [[ ! -d "$VENV_DIR" ]]; then
    echo "Virtual environment not found. Running installer..."
    "$SCRIPT_DIR/install_clipboard_server.sh"
fi

# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"

if pgrep -f "clipboard_server_advanced.py" >/dev/null 2>&1; then
    echo "Server already running (PID: $(pgrep -f 'clipboard_server_advanced.py'))"
    exit 0
fi

echo "Starting Advanced Clipboard MCP Server..."
echo "Audit log: $SCRIPT_DIR/clipboard_advanced_audit.log"
exec python "$SCRIPT_DIR/clipboard_server_advanced.py"
