#!/usr/bin/env bash
# Server Protection Pro — full production installer
# Installs CSF (if needed), MSV blocklist updater, API dashboard, systemd unit.
set -euo pipefail

OPT_DIR="/opt/server-protection"
BIN_DIR="/usr/local/bin"
LOG_DIR="/var/log/server-protection"
CRON_MSV="0 */2 * * *"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
log()  { echo -e "${GREEN}[$(date '+%F %T')]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()  { echo -e "${RED}[ERROR]${NC} $*"; }
info() { echo -e "${BLUE}[INFO]${NC} $*"; }

[[ $EUID -eq 0 ]] || { err "Run as root"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

detect_os() {
  if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    OS=$ID
    info "OS: $ID $VERSION_ID"
  else
    err "Cannot detect OS"; exit 1
  fi
}

install_pkgs() {
  log "Installing packages..."
  case $OS in
    ubuntu|debian)
      apt-get update -qq
      DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        curl wget perl ipset python3 \
        clamav clamav-daemon rkhunter 2>/dev/null || true
      ;;
    centos|rhel|rocky|almalinux|fedora)
      yum install -y curl wget perl ipset python3 clamav rkhunter 2>/dev/null || true
      ;;
    *)
      warn "Unknown OS — install curl wget perl ipset python3 manually"
      ;;
  esac
}

install_csf() {
  if command -v csf >/dev/null 2>&1; then
    info "CSF already installed: $(command -v csf)"
    return 0
  fi
  log "Installing CSF..."
  cd /tmp
  wget -q https://download.configserver.com/csf.tgz
  tar -xzf csf.tgz
  cd csf && sh install.sh
  cd / && rm -rf /tmp/csf /tmp/csf.tgz
  command -v csf >/dev/null 2>&1 || { err "CSF install failed"; exit 1; }
  log "CSF installed"
}

configure_csf() {
  log "Configuring CSF for ipset blocklists..."
  local conf=/etc/csf/csf.conf
  [[ -f $conf ]] || { err "csf.conf missing"; exit 1; }

  if grep -q '^LF_IPSET' "$conf"; then
    sed -i 's/^LF_IPSET = .*/LF_IPSET = "1"/' "$conf"
  else
    echo 'LF_IPSET = "1"' >> "$conf"
  fi
  grep -q '^LF_IPSET_HASHSIZE' "$conf" || echo 'LF_IPSET_HASHSIZE = "32768"' >> "$conf"
  grep -q '^LF_IPSET_MAXELEM' "$conf" || echo 'LF_IPSET_MAXELEM = "200000"' >> "$conf"
  log "LF_IPSET enabled"
}

install_files() {
  log "Installing to $OPT_DIR ..."
  mkdir -p "$OPT_DIR" "$OPT_DIR/scripts" "$OPT_DIR/dashboard" "$OPT_DIR/systemd" "$LOG_DIR"

  cp -f "$SCRIPT_DIR/server_protection_api.py" "$OPT_DIR/"
  cp -f "$SCRIPT_DIR/msv-blocklist-update.sh" "$OPT_DIR/"
  cp -f "$SCRIPT_DIR/monitor.sh" "$OPT_DIR/"
  cp -f "$SCRIPT_DIR/run_dashboard.sh" "$OPT_DIR/"
  cp -f "$SCRIPT_DIR/scripts/"*.sh "$OPT_DIR/scripts/"
  cp -f "$SCRIPT_DIR/dashboard/index.html" "$OPT_DIR/dashboard/"
  cp -f "$SCRIPT_DIR/systemd/server-protection.service" "$OPT_DIR/systemd/"
  cp -f "$SCRIPT_DIR/README.md" "$OPT_DIR/" 2>/dev/null || true

  chmod 755 "$OPT_DIR/server_protection_api.py"
  chmod 755 "$OPT_DIR"/*.sh "$OPT_DIR/scripts/"*.sh

  # Symlinks into PATH
  ln -sfn "$OPT_DIR/msv-blocklist-update.sh" "$BIN_DIR/msv-blocklist-update.sh"
  ln -sfn "$OPT_DIR/monitor.sh" "$BIN_DIR/msv-monitor.sh"
  ln -sfn "$OPT_DIR/run_dashboard.sh" "$BIN_DIR/server-protection-dashboard"

  log "Files installed"
}

setup_cron() {
  if crontab -l 2>/dev/null | grep -q msv-blocklist-update; then
    info "MSV cron already present"
  else
    (crontab -l 2>/dev/null; echo "$CRON_MSV $BIN_DIR/msv-blocklist-update.sh >> $LOG_DIR/msv-blocklist.log 2>&1") | crontab -
    log "Cron: MSV blocklist every 2 hours"
  fi
}

setup_systemd() {
  log "Installing systemd unit..."
  cp -f "$OPT_DIR/systemd/server-protection.service" /etc/systemd/system/server-protection.service
  systemctl daemon-reload
  systemctl enable server-protection.service
  systemctl restart server-protection.service || warn "Service start failed — check journalctl -u server-protection"
  sleep 1
  systemctl --no-pager --full status server-protection.service || true
}

run_msv_once() {
  log "Initial MSV blocklist update..."
  bash "$OPT_DIR/msv-blocklist-update.sh" || warn "MSV update reported errors (see $LOG_DIR)"
}

main() {
  echo ""
  echo "=============================================="
  echo "  Server Protection Pro — Production Install"
  echo "=============================================="
  echo ""

  detect_os
  install_pkgs
  install_csf
  configure_csf
  install_files
  setup_cron
  run_msv_once
  setup_systemd

  echo ""
  echo "=============================================="
  echo "  DONE"
  echo "  Dashboard:  http://127.0.0.1:8080/"
  echo "  Update:     sudo msv-blocklist-update.sh"
  echo "  Monitor:    sudo msv-monitor.sh"
  echo "  Service:    systemctl status server-protection"
  echo "  Logs:       $LOG_DIR/"
  echo "=============================================="
  echo ""
}

main
