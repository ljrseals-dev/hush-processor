# Server Protection Pro (Production)

Linux server hardening companion for DawChecker Secure Ultimate.

**Production only — no demo mode.** Requires CSF.

This toolkit is **not** part of the DawChecker desktop/VST3 audio app. It runs on a Linux host as root and never links into the audio binary.

---

## What you get

| Component | Path | Role |
|-----------|------|------|
| API + UI | `server_protection_api.py` + `dashboard/` | Live dashboard on `127.0.0.1:8080` |
| MSV blocklists | `msv-blocklist-update.sh` | CSF native `NAME\|INTERVAL\|MAX\|URL` updater |
| Scanners | `scripts/clamav-scan.sh`, `rkhunter-check.sh`, `aide-check.sh` | Real tool wrappers |
| CSF helper | `scripts/csf-sync.sh` | reload / restart / status |
| Monitor | `monitor.sh` | CLI security status |
| Systemd | `systemd/server-protection.service` | Persist the API |
| Installer | `install.sh` | One-shot production install |

---

## One-command install (server)

```bash
cd tools/server-protection
sudo ./install.sh
```

This will:

1. Install packages (python3, ipset, clamav, rkhunter, …)
2. Install CSF if missing
3. Enable `LF_IPSET` + size hints in `csf.conf`
4. Copy tree to `/opt/server-protection`
5. Symlink CLI tools into `/usr/local/bin`
6. Cron MSV updates every 2 hours
7. Run initial blocklist update
8. Enable + start `server-protection.service`

Open: **http://127.0.0.1:8080/**

---

## Manual run (no systemd)

```bash
sudo ./run_dashboard.sh
```

---

## API (real CSF only)

| Method | Path | Action |
|--------|------|--------|
| GET | `/api/status` | Score, ipset counts, root flag |
| GET | `/api/bans` | `/etc/csf/csf.deny` |
| GET | `/api/whitelist` | `/etc/csf/csf.allow` |
| GET | `/api/logs` | API event log |
| GET | `/api/jobs` | Background job state |
| POST | `/api/unblock` | `{ "ip" }` → `csf -dr` + allow |
| POST | `/api/whitelist` | `{ "entry" }` → `csf -a` |
| DELETE | `/api/whitelist` | `{ "entry" }` → `csf -ar` |
| POST | `/api/refresh` | `csf -r` |
| POST | `/api/scan/clamav` | Start ClamAV |
| POST | `/api/scan/rkhunter` | Start rkhunter |
| POST | `/api/panic` | Reload + scans + MSV update |
| POST | `/api/blocklist/update` | Run MSV script |

Without CSF the process **exits** at startup. Mutations require root (`SPP_REQUIRE_ROOT=1` default).

---

## Blocklist sources (MSV)

| Name | Interval | MAX | URL |
|------|----------|-----|-----|
| BITWIRE_IN | 7200s | 20000 | bitwire-it inbound |
| IPSUM_L3 | 86400s | all | stamparm ipsum level 3 |

Only CSF definition lines are written — lists are never dumped into `csf.blocklists`.

---

## Ops

```bash
sudo msv-blocklist-update.sh          # refresh feeds
sudo msv-monitor.sh                   # status dump
sudo systemctl status server-protection
journalctl -u server-protection -f
tail -f /var/log/server-protection/api.log
```

---

## Safety

- Bind address is loopback by default (`SPP_HOST=127.0.0.1`)
- Aggressive IP lists can false-positive — keep console access when first enabling
- Ensure SSH is allowed in CSF before remote `csf -ra`
