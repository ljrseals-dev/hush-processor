#!/usr/bin/env bash
# msv-blocklist-update.sh — Corrected Version
# Fetches bad IP lists via CSF's native mechanism (csf.blocklists)
# Does NOT dump raw IPs into csf.blocklists
# Run as root or with sudo
# Suggested cron: 0 */2 * * * /usr/local/bin/msv-blocklist-update.sh >> /var/log/msv-blocklist.log 2>&1
#
# Part of DawChecker Secure Ultimate — optional server-side hardening toolkit.
# This script is independent of the audio/GUI application and is never linked
# into or invoked by the desktop/VST3 binary.

set -euo pipefail

# Configuration
CSF_BLOCKLIST="/etc/csf/csf.blocklists"
CSF_CONF="/etc/csf/csf.conf"
LOG_FILE="/var/log/msv-blocklist.log"
BACKUP_DIR="/etc/csf/backups"
TEMP_FILE=$(mktemp)
TIMEOUT=30

# Color codes for output (only if terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG_FILE"
}

trap 'log "ERROR: Line $LINENO failed with exit code $?"' ERR

# Root check
if [[ $EUID -ne 0 ]]; then
    log "ERROR: This script must be run as root"
    exit 1
fi

# CSF check
if ! command -v csf &> /dev/null; then
    log "ERROR: CSF (ConfigServer Security & Firewall) is not installed"
    log "Install CSF first or run tools/server-protection/install.sh"
    exit 1
fi

check_ipset_enabled() {
    if [[ -f "$CSF_CONF" ]] && grep -q '^LF_IPSET = "1"' "$CSF_CONF"; then
        return 0
    else
        log "WARNING: LF_IPSET is not enabled in $CSF_CONF"
        log "Large blocklists will not work efficiently without ipset"
        return 1
    fi
}

create_backup() {
    log "Creating backup..."
    mkdir -p "$BACKUP_DIR"
    local backup_file="$BACKUP_DIR/csf.blocklists.bak.$(date +%F_%H%M%S)"
    if [[ -f "$CSF_BLOCKLIST" ]]; then
        cp "$CSF_BLOCKLIST" "$backup_file"
        log "Backup saved: $backup_file"
    else
        touch "$CSF_BLOCKLIST"
        log "Created empty $CSF_BLOCKLIST"
    fi
    # Keep only last 7 backups
    ls -t "$BACKUP_DIR"/csf.blocklists.bak.* 2>/dev/null | tail -n +8 | xargs -r rm -f
}

# Sources: NAME|INTERVAL|MAX|URL  (CSF native format only — no raw IP dumps)
SOURCES=(
    "BITWIRE_IN|7200|20000|https://raw.githubusercontent.com/bitwire-it/ipblocklist/main/inbound.txt"
    "IPSUM_L3|86400|0|https://raw.githubusercontent.com/stamparm/ipsum/master/levels/3.txt"
)

clean_old_definitions() {
    log "Cleaning old MSV definitions..."
    grep -vE '^(BITWIRE_IN|IPSUM_L3)\|' "$CSF_BLOCKLIST" > "$TEMP_FILE" 2>/dev/null || true
    # Also drop previous MSV comment banners
    grep -vE '^# MSV-SpyBlocker' "$TEMP_FILE" > "${TEMP_FILE}.2" 2>/dev/null || true
    mv "${TEMP_FILE}.2" "$CSF_BLOCKLIST"
}

add_definitions() {
    log "Adding CSF blocklist definitions..."
    {
        echo ""
        echo "# MSV-SpyBlocker: Virus/Malware/Bad IPs (updated $(date '+%Y-%m-%d %H:%M'))"
        for src in "${SOURCES[@]}"; do
            echo "$src"
        done
    } >> "$CSF_BLOCKLIST"
}

clear_fetched_lists() {
    log "Clearing cached CSF blocklist downloads..."
    rm -f /var/lib/csf/csf.block.BITWIRE_IN /var/lib/csf/csf.block.IPSUM_L3
    log "Cleared cached blocklist data"
}

restart_csf() {
    log "Restarting CSF..."
    if csf -ra; then
        log "CSF restart successful"
    else
        log "ERROR: CSF restart failed - restoring backup"
        local latest_backup
        latest_backup=$(ls -t "$BACKUP_DIR"/csf.blocklists.bak.* 2>/dev/null | head -1 || true)
        if [[ -n "${latest_backup:-}" ]]; then
            cp "$latest_backup" "$CSF_BLOCKLIST"
            csf -ra || true
            log "Restored from backup: $latest_backup"
        fi
        exit 1
    fi
}

allow_googlebot() {
    log "Allowing Googlebot ranges..."
    local googlebot_temp="/tmp/googlebot_$$.json"
    if wget -q --timeout="$TIMEOUT" \
        https://developers.google.com/search/apis/ipranges/googlebot.json \
        -O "$googlebot_temp" 2>/dev/null; then
        grep -Eo '((25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])(\/(3[0-2]|[12]?[0-9]))?' \
            "$googlebot_temp" \
            | sort -u \
            | while read -r ip; do
                csf -a "$ip" "Googlebot" 2>/dev/null || true
            done
        log "Googlebot ranges from official JSON added"
    else
        log "WARNING: Failed to fetch Googlebot JSON; adding known ranges only"
    fi
    csf -a 66.249.64.0/19 "Googlebot" 2>/dev/null || true
    csf -a 66.249.80.0/20 "Googlebot" 2>/dev/null || true
    rm -f "$googlebot_temp"
}

show_stats() {
    log "Collecting statistics..."
    echo ""
    echo "=== CSF Status (first 30 lines) ==="
    csf -s 2>/dev/null | head -30 || true

    echo ""
    echo "=== Recent BLOCK events ==="
    grep "BLOCK" /var/log/csf/csf.log 2>/dev/null | tail -20 || echo "No recent blocks"

    echo ""
    echo "=== ipset summary (if available) ==="
    if command -v ipset &> /dev/null; then
        ipset list -t 2>/dev/null | grep -E '^(Name|Number of entries)' | head -30 || true
    fi

    echo ""
    echo "=== Active MSV definitions ==="
    grep -E '^(BITWIRE_IN|IPSUM_L3)\|' "$CSF_BLOCKLIST" || echo "No definitions found"
}

main() {
    echo ""
    echo "========================================="
    echo "  MSV Blocklist Update (DawChecker tools)"
    echo "  $(date '+%Y-%m-%d %H:%M:%S')"
    echo "========================================="
    echo ""

    create_backup
    check_ipset_enabled || log "Continuing without ipset verification"
    clean_old_definitions
    add_definitions
    clear_fetched_lists
    restart_csf
    allow_googlebot
    show_stats

    echo ""
    echo "========================================="
    echo "  Update Complete"
    echo "  Log: $LOG_FILE"
    echo "========================================="
}

main
rm -f "$TEMP_FILE"
