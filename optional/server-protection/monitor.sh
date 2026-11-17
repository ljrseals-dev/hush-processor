#!/usr/bin/env bash
# monitor.sh — Security status monitor (optional DawChecker server toolkit)
# Checks CSF, Fail2Ban, SSH posture, blocklists, and basic resources.

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

warn()  { echo -e "${YELLOW}[WARNING]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
info()  { echo -e "${BLUE}[INFO]${NC} $*"; }

if [[ $EUID -ne 0 ]]; then
    warn "Not running as root — some checks may be limited"
fi

check_csf() {
    echo ""
    echo "=== CSF Firewall ==="
    if command -v csf &> /dev/null; then
        csf -s 2>/dev/null | head -20 || true
        echo ""
        echo "BLOCK events (log lines matching BLOCK):"
        grep -c "BLOCK" /var/log/csf/csf.log 2>/dev/null || echo "0"
    else
        error "CSF not installed"
    fi
}

check_fail2ban() {
    echo ""
    echo "=== Fail2Ban ==="
    if command -v fail2ban-client &> /dev/null; then
        fail2ban-client status 2>/dev/null || warn "Fail2Ban client present but status failed"
    else
        info "Fail2Ban not installed (optional)"
    fi
}

check_ssh() {
    echo ""
    echo "=== SSH Configuration ==="
    if [[ -f /etc/ssh/sshd_config ]]; then
        grep -E '^(PermitRootLogin|PasswordAuthentication|Port|MaxAuthTries)\s' /etc/ssh/sshd_config 2>/dev/null || \
            info "Using defaults (no explicit overrides found)"
    fi
}

check_blocklists() {
    echo ""
    echo "=== MSV Blocklist Status ==="
    if [[ -f /etc/csf/csf.blocklists ]]; then
        echo "Active definitions:"
        grep -E '^(BITWIRE_IN|IPSUM_L3)\|' /etc/csf/csf.blocklists 2>/dev/null || echo "  (none)"
    fi

    if command -v ipset &> /dev/null; then
        echo ""
        echo "ipset summary:"
        ipset list -t 2>/dev/null | grep -E '^(Name|Number of entries)' | head -20 || warn "No ipset entries"
    fi

    if [[ -f /var/log/msv-blocklist.log ]]; then
        echo ""
        echo "Last MSV update log lines:"
        tail -8 /var/log/msv-blocklist.log 2>/dev/null
    fi
}

check_resources() {
    echo ""
    echo "=== System Resources ==="
    if command -v free &> /dev/null; then
        free -h | head -2
    fi
    df -h / 2>/dev/null | tail -1 || true
}

main() {
    echo ""
    echo "=========================================="
    echo "  Server Security Monitor"
    echo "  DawChecker tools / server-protection"
    echo "  $(date '+%Y-%m-%d %H:%M:%S')"
    echo "=========================================="

    check_csf
    check_fail2ban
    check_ssh
    check_blocklists
    check_resources

    echo ""
    echo "=========================================="
    echo "  Monitor Complete"
    echo "=========================================="
    echo ""
}

main
