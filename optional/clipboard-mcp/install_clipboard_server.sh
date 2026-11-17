#!/usr/bin/env bash
# ============================================================================
# install_clipboard_server.sh
# Installs the Advanced Clipboard MCP server with venv, system deps,
# config, start script, and optional Claude Code registration.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$SCRIPT_DIR"
VENV_DIR="$INSTALL_DIR/.venv"
PYTHON_BIN="${PYTHON_BIN:-python3}"
LOG_FILE="$INSTALL_DIR/install.log"
CONFIG_FILE="$INSTALL_DIR/clipboard_advanced_config.json"
REQUIREMENTS_FILE="$INSTALL_DIR/requirements.txt"

if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BLUE=''; NC=''
fi

log()  { echo -e "${GREEN}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $*" | tee -a "$LOG_FILE"; }
warn() { echo -e "${YELLOW}[WARNING]${NC} $*" | tee -a "$LOG_FILE"; }
error(){ echo -e "${RED}[ERROR]${NC} $*" | tee -a "$LOG_FILE"; }
info() { echo -e "${BLUE}[INFO]${NC} $*" | tee -a "$LOG_FILE"; }

trap 'error "Installation failed at line $LINENO with exit code $?"' ERR

check_python() {
    log "Checking Python version..."
    if ! command -v "$PYTHON_BIN" &>/dev/null; then
        error "Python 3.8+ is required but not found"
        exit 1
    fi
    PYTHON_VERSION=$("$PYTHON_BIN" --version 2>&1 | awk '{print $2}')
    PYTHON_MAJOR=$(echo "$PYTHON_VERSION" | cut -d. -f1)
    PYTHON_MINOR=$(echo "$PYTHON_VERSION" | cut -d. -f2)
    if [[ $PYTHON_MAJOR -lt 3 ]] || [[ $PYTHON_MAJOR -eq 3 && $PYTHON_MINOR -lt 8 ]]; then
        error "Python 3.8+ required. Found: $PYTHON_VERSION"
        exit 1
    fi
    log "Python $PYTHON_VERSION detected"
}

create_venv() {
    log "Creating virtual environment..."
    if [[ -d "$VENV_DIR" ]]; then
        warn "Virtual environment already exists. Removing..."
        rm -rf "$VENV_DIR"
    fi
    "$PYTHON_BIN" -m venv "$VENV_DIR"
    log "Virtual environment created at: $VENV_DIR"
}

install_dependencies() {
    log "Installing Python dependencies..."
    # shellcheck disable=SC1091
    source "$VENV_DIR/bin/activate"
    pip install --upgrade pip setuptools wheel
    if [[ -f "$REQUIREMENTS_FILE" ]]; then
        pip install -r "$REQUIREMENTS_FILE"
    else
        pip install "mcp[cli]" pyperclip typing-extensions
    fi
    log "Dependencies installed successfully"
}

install_system_deps() {
    log "Checking system dependencies..."
    if [[ -f /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        OS=$ID
    else
        OS="unknown"
    fi

    if [[ "$OS" != "darwin" ]] && ! command -v xclip &>/dev/null; then
        warn "xclip not found. Attempting install..."
        case $OS in
            ubuntu|debian)
                sudo apt-get update -qq
                sudo apt-get install -y -qq xclip xsel || warn "xclip install failed"
                ;;
            centos|rhel|rocky|almalinux|fedora)
                sudo yum install -y xclip xsel || warn "xclip install failed"
                ;;
            arch|manjaro)
                sudo pacman -S --noconfirm xclip xsel || warn "xclip install failed"
                ;;
            *)
                warn "Unsupported OS. Install xclip manually."
                ;;
        esac
    fi

    if [[ "$OS" != "darwin" ]]; then
        if ! "$PYTHON_BIN" -c "import tkinter" 2>/dev/null; then
            warn "tkinter not found. Attempting install..."
            case $OS in
                ubuntu|debian) sudo apt-get install -y -qq python3-tk || true ;;
                centos|rhel|rocky|almalinux|fedora) sudo yum install -y python3-tkinter || true ;;
                *) warn "Install python3-tk manually for consent popup" ;;
            esac
        fi
    fi
    log "System dependencies ready"
}

create_config() {
    log "Creating default configuration..."
    if [[ -f "$CONFIG_FILE" ]]; then
        warn "Config exists — backing up"
        cp "$CONFIG_FILE" "$CONFIG_FILE.bak.$(date +%F_%H%M%S)"
    fi
    cat > "$CONFIG_FILE" << 'CFGEOF'
{
    "enabled": true,
    "consent_given": false,
    "log_content_preview": false,
    "preview_max_chars": 40,
    "cache_enabled": true,
    "cache_max_entries": 100,
    "history_enabled": true,
    "history_max_entries": 50,
    "batch_mode": true,
    "smart_detection": true,
    "performance_mode": "balanced",
    "auto_clear_sensitive": true,
    "sensitive_patterns": [
        "\\b\\d{3}-\\d{2}-\\d{4}\\b",
        "\\b\\d{16}\\b",
        "password\\s*[:=]\\s*\\S+",
        "token\\s*[:=]\\s*\\S+",
        "api[_-]?key\\s*[:=]\\s*\\S+",
        "BEGIN\\s+(RSA|OPENSSH|EC)\\s+PRIVATE\\s+KEY",
        "AKIA[0-9A-Z]{16}",
        "gh[pousr]_[A-Za-z0-9]{36,}",
        "sk-[a-zA-Z0-9]{20,}",
        "xox[baprs]-[a-zA-Z0-9-]{10,}",
        "-----BEGIN.*?PRIVATE KEY-----"
    ]
}
CFGEOF
    log "Config created: $CONFIG_FILE"
}

setup_claude_code() {
    log "Setting up Claude Code integration..."
    if command -v claude &>/dev/null; then
        claude mcp remove clipboard-advanced 2>/dev/null || true
        claude mcp add clipboard-advanced -- "$VENV_DIR/bin/python" "$INSTALL_DIR/clipboard_server_advanced.py"
        log "Claude Code integration configured"
        info "Test with: claude mcp test clipboard-advanced"
    else
        warn "Claude Code not found"
        info "Install: npm install -g @anthropic-ai/claude-code"
        info "Then: claude mcp add clipboard-advanced -- $VENV_DIR/bin/python $INSTALL_DIR/clipboard_server_advanced.py"
    fi
}

create_start_script() {
    log "Creating start script..."
    cat > "$INSTALL_DIR/start_clipboard_server.sh" << 'STARTEOF'
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
STARTEOF
    chmod +x "$INSTALL_DIR/start_clipboard_server.sh"
    log "Start script created"
}

create_requirements() {
    log "Writing requirements.txt..."
    cat > "$REQUIREMENTS_FILE" << 'REQEOF'
mcp[cli]>=1.0.0
pyperclip>=1.8.2
typing-extensions>=4.8.0
REQEOF
    log "Requirements file ready"
}

verify_installation() {
    log "Verifying installation..."
    # shellcheck disable=SC1091
    source "$VENV_DIR/bin/activate"
    python -c "
import sys
sys.path.insert(0, '$INSTALL_DIR')
from clipboard_server_advanced import clipboard_server
print('Server classes loaded')
print('cache=', len(clipboard_server.cache))
print('history=', len(clipboard_server.history.entries))
print('mode=', clipboard_server.config.get('performance_mode'))
" || { error "Server import failed"; exit 1; }

    python -c "
import pyperclip
pyperclip.copy('dawchecker-clipboard-test')
assert pyperclip.paste() == 'dawchecker-clipboard-test'
print('Clipboard access OK')
" 2>/dev/null || warn "Clipboard round-trip failed (install xclip/xsel on Linux)"

    log "Installation verified"
}

main() {
    echo ""
    echo "============================================"
    echo "  Advanced Clipboard MCP Server - Installer"
    echo "============================================"
    echo ""
    : > "$LOG_FILE"
    check_python
    create_requirements
    create_venv
    install_dependencies
    install_system_deps
    create_config
    create_start_script
    setup_claude_code
    verify_installation
    echo ""
    echo "============================================"
    echo "  DONE"
    echo "  Start:  ./start_clipboard_server.sh"
    echo "  Venv:   $VENV_DIR"
    echo "  Config: $CONFIG_FILE"
    echo "  Log:    $LOG_FILE"
    echo "============================================"
    echo ""
}

main
