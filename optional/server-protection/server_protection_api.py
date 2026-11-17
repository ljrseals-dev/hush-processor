#!/usr/bin/env python3
"""
Server Protection Pro — production API (no demo mode, no simulated data).

Requires CSF on the host. Binds to 127.0.0.1:8080 by default.
Run as root for deny/allow mutations.

  sudo python3 server_protection_api.py
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import threading
import time
from datetime import datetime, timezone
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

HOST = os.environ.get("SPP_HOST", "127.0.0.1")
PORT = int(os.environ.get("SPP_PORT", "8080"))
ROOT = Path(__file__).resolve().parent
DASHBOARD_DIR = ROOT / "dashboard"
SCRIPTS_DIR = ROOT / "scripts"
LOG_PATH = Path(os.environ.get("SPP_LOG", "/var/log/server-protection/api.log"))

REQUIRE_ROOT_FOR_MUTATIONS = os.environ.get("SPP_REQUIRE_ROOT", "1") != "0"

_LOG: list[dict[str, str]] = []
_LOG_LOCK = threading.Lock()
_JOBS: dict[str, dict[str, Any]] = {}
_JOBS_LOCK = threading.Lock()

IPV4_CIDR = re.compile(
    r"^(?:(?:25[0-5]|2[0-4]\d|[01]?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|[01]?\d?\d)"
    r"(?:/(?:3[0-2]|[12]?\d))?$"
)
DOMAIN_RE = re.compile(r"^(?=.{1,253}$)(?!-)[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$")


def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def add_log(msg: str, level: str = "info") -> None:
    entry = {"ts": _now(), "level": level, "msg": msg}
    with _LOG_LOCK:
        _LOG.append(entry)
        if len(_LOG) > 1000:
            del _LOG[: len(_LOG) - 1000]
    try:
        LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
        with LOG_PATH.open("a", encoding="utf-8") as f:
            f.write(f"{entry['ts']} [{level}] {msg}\n")
    except OSError:
        pass


def which(cmd: str) -> str | None:
    return shutil.which(cmd)


def run_cmd(argv: list[str], timeout: int = 120) -> tuple[int, str, str]:
    try:
        p = subprocess.run(
            argv, capture_output=True, text=True, timeout=timeout, check=False
        )
        return p.returncode, (p.stdout or "").strip(), (p.stderr or "").strip()
    except FileNotFoundError:
        return 127, "", f"{argv[0]} not found"
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"


def require_csf() -> None:
    if not which("csf"):
        raise RuntimeError("CSF (csf) is not installed or not on PATH")


def is_root() -> bool:
    return hasattr(os, "geteuid") and os.geteuid() == 0


def validate_entry(entry: str) -> tuple[bool, str]:
    entry = entry.strip()
    if not entry or len(entry) > 253:
        return False, "empty or too long"
    if IPV4_CIDR.match(entry):
        return True, "ip"
    if DOMAIN_RE.match(entry):
        return True, "domain"
    return False, "must be IPv4, CIDR, or domain"


def read_csf_file(path: str, limit: int = 5000) -> list[str]:
    entries: list[str] = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            tok = line.split()[0]
            entries.append(tok)
            if len(entries) >= limit:
                break
    return entries


def ipset_entry_counts() -> dict[str, int]:
    """Return {set_name: entry_count} from ipset list -t."""
    if not which("ipset"):
        return {}
    code, out, err = run_cmd(["ipset", "list", "-t"], timeout=20)
    if code != 0:
        add_log(f"[ipset] list failed: {err}", "warn")
        return {}
    counts: dict[str, int] = {}
    name = None
    for line in out.splitlines():
        if line.startswith("Name:"):
            name = line.split(":", 1)[1].strip()
        elif line.startswith("Number of entries:") and name:
            try:
                counts[name] = int(line.split(":")[1].strip())
            except ValueError:
                counts[name] = 0
            name = None
    return counts


def blocked_total() -> int:
    counts = ipset_entry_counts()
    if counts:
        return min(sum(counts.values()), 2_000_000)
    # Fallback: lines in csf.deny
    try:
        return len(read_csf_file("/etc/csf/csf.deny", 100_000))
    except OSError:
        return 0


def get_bans() -> list[dict[str, Any]]:
    require_csf()
    path = "/etc/csf/csf.deny"
    if not os.path.isfile(path):
        return []
    bans = []
    for ip in read_csf_file(path, 2000):
        bans.append(
            {
                "ip": ip,
                "cc": "",
                "org": "csf.deny",
                "set": "CSF_DENY",
                "hits": 0,
                "active": True,
            }
        )
    return bans


def get_whitelist() -> list[dict[str, str]]:
    require_csf()
    path = "/etc/csf/csf.allow"
    if not os.path.isfile(path):
        return []
    out = []
    for e in read_csf_file(path, 2000):
        typ = "cidr" if "/" in e else ("ip" if e[:1].isdigit() else "domain")
        out.append({"entry": e, "type": typ})
    return out


def status_payload() -> dict[str, Any]:
    require_csf()
    counts = ipset_entry_counts()
    blocked = min(sum(counts.values()), 2_000_000) if counts else blocked_total()
    # Prefer named MSV sets if present
    bitwire = 0
    ipsum = 0
    for name, n in counts.items():
        u = name.upper()
        if "BITWIRE" in u:
            bitwire += n
        if "IPSUM" in u:
            ipsum += n
    score = min(100, int(blocked / 200)) if blocked else 0
    return {
        "ok": True,
        "ts": _now(),
        "csf": True,
        "root": is_root(),
        "blocked": blocked,
        "score": score,
        "score_label": (
            "CRITICAL" if score >= 80 else
            "ELEVATED" if score >= 50 else
            "MODERATE" if score >= 20 else
            "LOW"
        ),
        "ipset_sets": counts,
        "bitwire_estimate": bitwire,
        "ipsum_estimate": ipsum,
        "jobs": {
            k: {"state": v.get("state"), "detail": v.get("detail"), "rc": v.get("rc")}
            for k, v in _JOBS.items()
        },
    }


def guard_mutation() -> None:
    require_csf()
    if REQUIRE_ROOT_FOR_MUTATIONS and not is_root():
        raise PermissionError("root required for CSF mutations (or set SPP_REQUIRE_ROOT=0)")


def csf_allow(entry: str) -> tuple[bool, str]:
    guard_mutation()
    ok, kind = validate_entry(entry)
    if not ok:
        return False, kind
    code, out, err = run_cmd(["csf", "-a", entry, "ServerProtectionPro"], timeout=45)
    msg = out or err or f"rc={code}"
    add_log(f"[csf] -a {entry} → {code} {msg[:200]}")
    return code == 0, msg


def csf_allow_remove(entry: str) -> tuple[bool, str]:
    guard_mutation()
    ok, kind = validate_entry(entry)
    if not ok:
        return False, kind
    code, out, err = run_cmd(["csf", "-ar", entry], timeout=45)
    msg = out or err or f"rc={code}"
    add_log(f"[csf] -ar {entry} → {code} {msg[:200]}")
    return code == 0, msg


def csf_deny_remove(ip: str) -> tuple[bool, str]:
    guard_mutation()
    ok, kind = validate_entry(ip)
    if not ok:
        return False, kind
    code, out, err = run_cmd(["csf", "-dr", ip], timeout=45)
    msg = out or err or f"rc={code}"
    add_log(f"[csf] -dr {ip} → {code} {msg[:200]}")
    return code == 0, msg


def csf_reload() -> tuple[bool, str]:
    guard_mutation()
    code, out, err = run_cmd(["csf", "-r"], timeout=90)
    msg = out or err or f"rc={code}"
    add_log(f"[csf] -r → {code}")
    return code == 0, msg[:800]


def start_job(name: str, argv: list[str], timeout: int = 600) -> None:
    with _JOBS_LOCK:
        if _JOBS.get(name, {}).get("state") == "running":
            raise RuntimeError(f"job {name} already running")
        _JOBS[name] = {"state": "running", "detail": "started", "started": _now()}

    def worker() -> None:
        add_log(f"[{name}] exec: {' '.join(argv)}")
        code, out, err = run_cmd(argv, timeout=timeout)
        detail = (out or err or f"rc={code}")[-600:]
        with _JOBS_LOCK:
            _JOBS[name] = {
                "state": "ok" if code == 0 else "error",
                "detail": detail,
                "finished": _now(),
                "rc": code,
            }
        add_log(f"[{name}] done rc={code}")

    threading.Thread(target=worker, daemon=True).start()


def trigger_clamav() -> dict[str, Any]:
    script = SCRIPTS_DIR / "clamav-scan.sh"
    if script.is_file():
        start_job("clamav", ["bash", str(script)])
        return {"ok": True, "job": "clamav"}
    if which("clamdscan"):
        start_job("clamav", ["clamdscan", "--fdpass", "/tmp"])
        return {"ok": True, "job": "clamav"}
    if which("clamscan"):
        start_job("clamav", ["clamscan", "-r", "--infected", "/tmp"])
        return {"ok": True, "job": "clamav"}
    raise RuntimeError("ClamAV not installed (need clamscan/clamdscan or scripts/clamav-scan.sh)")


def trigger_rkhunter() -> dict[str, Any]:
    script = SCRIPTS_DIR / "rkhunter-check.sh"
    if script.is_file():
        start_job("rkhunter", ["bash", str(script)])
        return {"ok": True, "job": "rkhunter"}
    if which("rkhunter"):
        start_job("rkhunter", ["rkhunter", "--check", "--sk", "--nocolors"])
        return {"ok": True, "job": "rkhunter"}
    raise RuntimeError("rkhunter not installed")


def trigger_msv_blocklist() -> dict[str, Any]:
    msv = ROOT / "msv-blocklist-update.sh"
    if not msv.is_file():
        raise RuntimeError("msv-blocklist-update.sh not found")
    guard_mutation()
    start_job("msv_blocklist", ["bash", str(msv)], timeout=300)
    return {"ok": True, "job": "msv_blocklist"}


def trigger_panic() -> dict[str, Any]:
    guard_mutation()
    add_log("PANIC invoked")
    results: dict[str, Any] = {}
    ok, msg = csf_reload()
    results["csf_reload"] = {"ok": ok, "msg": msg}
    try:
        results["clamav"] = trigger_clamav()
    except Exception as e:
        results["clamav"] = {"ok": False, "error": str(e)}
    try:
        results["rkhunter"] = trigger_rkhunter()
    except Exception as e:
        results["rkhunter"] = {"ok": False, "error": str(e)}
    try:
        results["msv_blocklist"] = trigger_msv_blocklist()
    except Exception as e:
        results["msv_blocklist"] = {"ok": False, "error": str(e)}
    return {"ok": True, "results": results}


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args: Any, **kwargs: Any) -> None:
        super().__init__(*args, directory=str(DASHBOARD_DIR), **kwargs)

    def log_message(self, fmt: str, *args: Any) -> None:
        add_log(f"[http] {self.address_string()} {fmt % args}")

    def _json(self, code: int, payload: Any) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict[str, Any]:
        n = int(self.headers.get("Content-Length") or 0)
        if n <= 0:
            return {}
        try:
            data = json.loads(self.rfile.read(n).decode("utf-8"))
            return data if isinstance(data, dict) else {}
        except json.JSONDecodeError:
            return {}

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        try:
            if path in ("/api/status", "/status"):
                self._json(200, status_payload())
                return
            if path in ("/api/bans", "/bans"):
                self._json(200, {"ok": True, "bans": get_bans()})
                return
            if path in ("/api/whitelist", "/whitelist"):
                self._json(200, {"ok": True, "whitelist": get_whitelist()})
                return
            if path in ("/api/logs", "/logs"):
                with _LOG_LOCK:
                    logs = list(_LOG[-200:])
                self._json(200, {"ok": True, "logs": logs})
                return
            if path in ("/api/jobs", "/jobs"):
                with _JOBS_LOCK:
                    jobs = dict(_JOBS)
                self._json(200, {"ok": True, "jobs": jobs})
                return
        except PermissionError as e:
            self._json(403, {"ok": False, "error": str(e)})
            return
        except RuntimeError as e:
            self._json(503, {"ok": False, "error": str(e)})
            return
        except OSError as e:
            self._json(500, {"ok": False, "error": str(e)})
            return

        if path == "/":
            self.path = "/index.html"
        return SimpleHTTPRequestHandler.do_GET(self)

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        data = self._read_json()
        try:
            if path in ("/api/unblock", "/unblock"):
                ip = str(data.get("ip", "")).strip()
                if not ip:
                    self._json(400, {"ok": False, "error": "ip required"})
                    return
                ok, msg = csf_deny_remove(ip)
                if ok:
                    csf_allow(ip)
                self._json(200 if ok else 500, {"ok": ok, "msg": msg, "ip": ip})
                return

            if path in ("/api/whitelist", "/whitelist"):
                entry = str(data.get("entry", "")).strip()
                if not entry:
                    self._json(400, {"ok": False, "error": "entry required"})
                    return
                ok, msg = csf_allow(entry)
                self._json(200 if ok else 500, {"ok": ok, "msg": msg, "entry": entry})
                return

            if path in ("/api/refresh", "/refresh"):
                ok, msg = csf_reload()
                self._json(200 if ok else 500, {"ok": ok, "msg": msg})
                return

            if path in ("/api/scan/clamav", "/scan/clamav"):
                self._json(200, trigger_clamav())
                return

            if path in ("/api/scan/rkhunter", "/scan/rkhunter"):
                self._json(200, trigger_rkhunter())
                return

            if path in ("/api/panic", "/panic"):
                self._json(200, trigger_panic())
                return

            if path in ("/api/blocklist/update", "/blocklist/update"):
                self._json(200, trigger_msv_blocklist())
                return

            self._json(404, {"ok": False, "error": "unknown endpoint"})
        except PermissionError as e:
            self._json(403, {"ok": False, "error": str(e)})
        except RuntimeError as e:
            self._json(503, {"ok": False, "error": str(e)})
        except OSError as e:
            self._json(500, {"ok": False, "error": str(e)})

    def do_DELETE(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        data = self._read_json()
        try:
            if path in ("/api/whitelist", "/whitelist"):
                entry = str(data.get("entry", "")).strip()
                if not entry:
                    self._json(400, {"ok": False, "error": "entry required"})
                    return
                ok, msg = csf_allow_remove(entry)
                self._json(200 if ok else 500, {"ok": ok, "msg": msg, "entry": entry})
                return
            self._json(404, {"ok": False, "error": "unknown endpoint"})
        except PermissionError as e:
            self._json(403, {"ok": False, "error": str(e)})
        except RuntimeError as e:
            self._json(503, {"ok": False, "error": str(e)})


def main() -> None:
    if not DASHBOARD_DIR.is_dir():
        raise SystemExit(f"Missing dashboard dir: {DASHBOARD_DIR}")

    # Hard requirement at startup
    if not which("csf"):
        raise SystemExit(
            "FATAL: CSF not found. Install ConfigServer Security & Firewall first.\n"
            "This build has no demo mode."
        )

    add_log(f"API start http://{HOST}:{PORT}/ root={is_root()}")
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"Server Protection Pro (production)  http://{HOST}:{PORT}/")
    print(f"CSF: yes | root: {is_root()} | mutations require root: {REQUIRE_ROOT_FOR_MUTATIONS}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nshutdown")
        httpd.shutdown()


if __name__ == "__main__":
    main()
