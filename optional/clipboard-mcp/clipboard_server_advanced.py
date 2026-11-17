"""
clipboard_server_advanced.py

Advanced MCP server for system clipboard access with caching, batch ops,
consent, sensitive-data detection, history, and performance metrics.

Dependencies:
    pip install "mcp[cli]" pyperclip
    Linux: sudo apt install xclip   (or xsel)
    Optional GUI consent: python3-tk

Run:
    python3 clipboard_server_advanced.py

Register (Claude Code example):
    claude mcp add clipboard-advanced -- python3 clipboard_server_advanced.py

This is an optional developer companion under tools/clipboard-mcp.
It is NOT linked into the DawChecker audio/GUI binary.
"""

from __future__ import annotations

import asyncio
import datetime
import hashlib
import json
import os
import re
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Tuple

import pyperclip
from mcp.server import Server
from mcp.server.stdio import stdio_server

# ---------------------------------------------------------------------------
# Paths (relative to process CWD; prefer tools/clipboard-mcp as CWD)
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent
CONFIG_FILE = str(ROOT / "clipboard_advanced_config.json")
AUDIT_LOG_FILE = str(ROOT / "clipboard_advanced_audit.log")
CACHE_FILE = str(ROOT / "clipboard_cache.json")
HISTORY_FILE = str(ROOT / "clipboard_history.json")

DEFAULT_CONFIG: Dict[str, Any] = {
    "enabled": True,
    "consent_given": False,
    "log_content_preview": False,
    "preview_max_chars": 40,
    "cache_enabled": True,
    "cache_max_entries": 100,
    "history_enabled": True,
    "history_max_entries": 50,
    "batch_mode": True,
    "smart_detection": True,
    "performance_mode": "balanced",  # fast | balanced | secure
    "auto_clear_sensitive": True,
    "sensitive_patterns": [
        r"\b\d{3}-\d{2}-\d{4}\b",
        r"\b\d{16}\b",
        r"password\s*[:=]\s*\S+",
        r"token\s*[:=]\s*\S+",
        r"api[_-]?key\s*[:=]\s*\S+",
        r"BEGIN\s+(RSA|OPENSSH|EC)\s+PRIVATE\s+KEY",
        r"AKIA[0-9A-Z]{16}",
        r"gh[pousr]_[A-Za-z0-9]{36,}",
        r"sk-[a-zA-Z0-9]{20,}",
        r"xox[baprs]-[a-zA-Z0-9-]{10,}",
        r"-----BEGIN.*?PRIVATE KEY-----",
    ],
}

_config_lock = threading.Lock()
_cache_lock = threading.Lock()
_history_lock = threading.Lock()

app = Server("clipboard-server-advanced")


@dataclass
class ClipboardCache:
    content: str
    hash: str
    timestamp: float
    size: int
    access_count: int = 0


class ClipboardHistory:
    def __init__(self, max_entries: int = 50) -> None:
        self.entries: deque = deque(maxlen=max_entries)
        self.max_entries = max_entries

    def add(self, content: str, content_type: str = "text") -> None:
        self.entries.appendleft(
            {
                "content": content[:500],
                "type": content_type,
                "timestamp": datetime.datetime.now().isoformat(),
                "hash": hashlib.sha256(content.encode("utf-8", errors="replace")).hexdigest()[:12],
            }
        )

    def get_recent(self, n: int = 10) -> List[Dict[str, Any]]:
        return list(self.entries)[:n]


class ClipboardPerformanceMonitor:
    def __init__(self) -> None:
        self.stats = {
            "total_ops": 0,
            "total_time": 0.0,
            "avg_time": 0.0,
            "cache_hits": 0,
            "cache_misses": 0,
            "batch_ops": 0,
            "errors": 0,
            "start_time": time.time(),
        }

    def update(
        self,
        operation_time: float,
        cache_hit: bool = False,
        batch: bool = False,
        error: bool = False,
    ) -> None:
        s = self.stats
        s["total_ops"] += 1
        s["total_time"] += operation_time
        s["avg_time"] = s["total_time"] / s["total_ops"]
        if cache_hit:
            s["cache_hits"] += 1
        else:
            s["cache_misses"] += 1
        if batch:
            s["batch_ops"] += 1
        if error:
            s["errors"] += 1

    def get_report(self) -> str:
        s = self.stats
        hit_rate = (s["cache_hits"] / s["total_ops"] * 100) if s["total_ops"] else 0.0
        uptime = time.time() - s["start_time"]
        ops_per_min = (s["total_ops"] / uptime * 60) if uptime > 0 else 0.0
        return (
            f"Performance Statistics\n"
            f"├─ Total operations: {s['total_ops']}\n"
            f"├─ Average time: {s['avg_time']*1000:.2f}ms\n"
            f"├─ Cache hit rate: {hit_rate:.1f}%\n"
            f"├─ Cache hits: {s['cache_hits']}\n"
            f"├─ Cache misses: {s['cache_misses']}\n"
            f"├─ Batch operations: {s['batch_ops']}\n"
            f"├─ Errors: {s['errors']}\n"
            f"├─ Uptime: {uptime:.1f}s\n"
            f"└─ Ops/min: {ops_per_min:.1f}"
        )


class ClipboardServer:
    def __init__(self) -> None:
        self.config = self._load_config()
        self.cache: Dict[str, ClipboardCache] = {}
        self.history = ClipboardHistory(int(self.config.get("history_max_entries", 50)))
        self.performance = ClipboardPerformanceMonitor()
        self._load_cache()
        self._load_history()

    def _load_config(self) -> dict:
        with _config_lock:
            try:
                with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                    cfg = json.load(f)
            except (FileNotFoundError, json.JSONDecodeError):
                cfg = {}
            return {**DEFAULT_CONFIG, **cfg}

    def _save_config(self) -> None:
        with _config_lock:
            with open(CONFIG_FILE, "w", encoding="utf-8") as f:
                json.dump(self.config, f, indent=2)

    def _load_cache(self) -> None:
        try:
            with open(CACHE_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
            for key, value in data.items():
                self.cache[key] = ClipboardCache(**value)
        except (FileNotFoundError, json.JSONDecodeError, TypeError):
            pass

    def _save_cache(self) -> None:
        if not self.config.get("cache_enabled", True):
            return
        with _cache_lock:
            data = {
                k: {
                    "content": v.content,
                    "hash": v.hash,
                    "timestamp": v.timestamp,
                    "size": v.size,
                    "access_count": v.access_count,
                }
                for k, v in self.cache.items()
            }
            with open(CACHE_FILE, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)

    def _load_history(self) -> None:
        try:
            with open(HISTORY_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.history.entries = deque(data, maxlen=self.history.max_entries)
        except (FileNotFoundError, json.JSONDecodeError):
            pass

    def _save_history(self) -> None:
        if not self.config.get("history_enabled", True):
            return
        with _history_lock:
            with open(HISTORY_FILE, "w", encoding="utf-8") as f:
                json.dump(list(self.history.entries), f, indent=2)

    def _detect_content_type(self, content: str) -> str:
        if not content:
            return "empty"
        if re.match(r"^https?://", content):
            return "url"
        code_patterns = [
            r"^\s*(def|class|import|from|function|var|let|const)\s",
            r"^\s*#include\s",
            r"^\s*<html|<!DOCTYPE",
            r"^\s*(SELECT|INSERT|UPDATE|DELETE)\s",
            r"^\s*(docker|kubectl|git)\s",
            r"^\s*(npm|yarn|pip|apt|yum)\s",
        ]
        for pattern in code_patterns:
            if re.search(pattern, content, re.MULTILINE):
                return "code"
        try:
            json.loads(content)
            return "json"
        except Exception:
            pass
        if re.match(r"^\s*[\w-]+:\s*\S+$", content, re.MULTILINE):
            return "yaml"
        if "\n" in content and len(content.split("\n")) > 2:
            return "multiline"
        if re.match(r"^[\w.\-]+@[\w.\-]+\.\w+$", content):
            return "email"
        if re.match(r"^[a-zA-Z]:[\\/]|^/", content):
            return "filepath"
        if re.match(r"^\d{1,3}(?:\.\d{1,3}){3}$", content):
            return "ip"
        if re.match(r"^-?\d+(?:\.\d+)?$", content):
            return "number"
        if content.lower() in ("true", "false"):
            return "boolean"
        if re.match(r"^(sudo|ls|cd|mkdir|rm|cp|mv|cat|grep|find)\s", content):
            return "shell"
        return "text"

    def _detect_sensitive_content(self, content: str) -> Tuple[bool, List[str]]:
        if not self.config.get("auto_clear_sensitive", True):
            return False, []
        detected: List[str] = []
        for pattern in self.config.get("sensitive_patterns", []):
            try:
                if re.search(pattern, content, re.IGNORECASE | re.DOTALL):
                    detected.append(pattern)
            except re.error:
                continue
        return bool(detected), detected

    def _audit_log(self, action: str, status: str, detail: str = "") -> None:
        line = f"{datetime.datetime.now().isoformat(timespec='seconds')}\t{action}\t{status}\t{detail}\n"
        try:
            with open(AUDIT_LOG_FILE, "a", encoding="utf-8") as f:
                f.write(line)
        except OSError:
            pass

    def _update_performance(
        self,
        operation_time: float,
        cache_hit: bool = False,
        batch: bool = False,
        error: bool = False,
    ) -> None:
        self.performance.update(operation_time, cache_hit, batch, error)


clipboard_server = ClipboardServer()


def show_consent_popup() -> bool:
    try:
        import tkinter as tk
        from tkinter import messagebox

        text = (
            "Advanced Clipboard Access Request\n\n"
            "This MCP server can read/write the system clipboard for AI tooling.\n"
            "Features: cache, history, type detection, sensitive-pattern checks.\n\n"
            "Clipboard data may be sent to connected AI services when used in prompts.\n"
            "Do you consent?"
        )
        root = tk.Tk()
        root.withdraw()
        root.attributes("-topmost", True)
        result = messagebox.askyesno("Advanced Clipboard Access", text, parent=root)
        root.destroy()
        return bool(result)
    except Exception as e:
        clipboard_server._audit_log("consent", "error", f"popup unavailable: {e}")
        return False


def ensure_consent() -> Tuple[bool, str]:
    if clipboard_server.config.get("consent_given"):
        return True, ""
    if show_consent_popup():
        clipboard_server.config["consent_given"] = True
        clipboard_server._save_config()
        clipboard_server._audit_log("consent", "success", "granted")
        return True, ""
    clipboard_server._audit_log("consent", "denied", "declined")
    return False, (
        "Consent required. Set \"consent_given\": true in "
        f"{CONFIG_FILE} or accept the dialog."
    )


@app.tool()
async def read_clipboard_fast() -> str:
    """High-speed clipboard read with caching, type detection, and sensitive checks."""
    t0 = time.perf_counter()
    cfg = clipboard_server.config
    if not cfg.get("enabled", True):
        clipboard_server._audit_log("read", "denied", "disabled")
        return "Clipboard access is disabled."
    ok, msg = ensure_consent()
    if not ok:
        return msg
    try:
        content = pyperclip.paste() or ""
        content_hash = hashlib.sha256(content.encode("utf-8", errors="replace")).hexdigest()
        cache_hit = False
        if cfg.get("cache_enabled") and content_hash in clipboard_server.cache:
            cache_hit = True
            clipboard_server.cache[content_hash].access_count += 1
        if not content:
            clipboard_server._audit_log("read", "empty")
            clipboard_server._update_performance(time.perf_counter() - t0, cache_hit)
            return "(Clipboard is empty)"
        ctype = clipboard_server._detect_content_type(content)
        sensitive, patterns = clipboard_server._detect_sensitive_content(content)
        if cfg.get("cache_enabled"):
            clipboard_server.cache[content_hash] = ClipboardCache(
                content=content[:1000],
                hash=content_hash,
                timestamp=time.time(),
                size=len(content),
                access_count=1,
            )
            max_entries = int(cfg.get("cache_max_entries", 100))
            if len(clipboard_server.cache) > max_entries:
                oldest = sorted(
                    clipboard_server.cache.keys(),
                    key=lambda k: clipboard_server.cache[k].timestamp,
                )[: max(1, len(clipboard_server.cache) - max_entries)]
                for k in oldest:
                    del clipboard_server.cache[k]
            clipboard_server._save_cache()
        if cfg.get("history_enabled"):
            clipboard_server.history.add(content, ctype)
            clipboard_server._save_history()
        preview = ""
        if cfg.get("log_content_preview"):
            n = int(cfg.get("preview_max_chars", 40))
            preview = content[:n].replace("\n", " ")
        clipboard_server._audit_log(
            "read",
            "success",
            f"len={len(content)} type={ctype}" + (f' preview="{preview}"' if preview else ""),
        )
        dt = time.perf_counter() - t0
        clipboard_server._update_performance(dt, cache_hit)
        lines = [
            f"Clipboard ({ctype}, {len(content)} chars)",
            "",
            "```",
            content[:2000] + ("..." if len(content) > 2000 else ""),
            "```",
            "",
            f"type={ctype} size={len(content)} hash={content_hash[:12]} "
            f"cache={'HIT' if cache_hit else 'MISS'} time_ms={dt*1000:.2f}",
        ]
        if sensitive:
            lines.append(f"SENSITIVE patterns matched: {len(patterns)} — use clear_sensitive_clipboard()")
        return "\n".join(lines)
    except Exception as e:
        clipboard_server._update_performance(time.perf_counter() - t0, error=True)
        clipboard_server._audit_log("read", "error", str(e))
        return f"Error reading clipboard: {e}"


@app.tool()
async def write_clipboard_fast(text: str) -> str:
    """Write text to the system clipboard with type/sensitive detection."""
    t0 = time.perf_counter()
    cfg = clipboard_server.config
    if not cfg.get("enabled", True):
        return "Clipboard access is disabled."
    ok, msg = ensure_consent()
    if not ok:
        return msg
    if not text:
        return "Cannot write empty text."
    try:
        ctype = clipboard_server._detect_content_type(text)
        sensitive, patterns = clipboard_server._detect_sensitive_content(text)
        pyperclip.copy(text)
        if cfg.get("cache_enabled"):
            h = hashlib.sha256(text.encode("utf-8", errors="replace")).hexdigest()
            clipboard_server.cache[h] = ClipboardCache(
                content=text[:1000], hash=h, timestamp=time.time(), size=len(text), access_count=1
            )
            clipboard_server._save_cache()
        if cfg.get("history_enabled"):
            clipboard_server.history.add(text, ctype)
            clipboard_server._save_history()
        clipboard_server._audit_log("write", "success", f"len={len(text)} type={ctype}")
        dt = time.perf_counter() - t0
        clipboard_server._update_performance(dt)
        out = f"Copied type={ctype} size={len(text)} time_ms={dt*1000:.2f}"
        if sensitive:
            out += f" | SENSITIVE matches={len(patterns)}"
        return out
    except Exception as e:
        clipboard_server._update_performance(time.perf_counter() - t0, error=True)
        clipboard_server._audit_log("write", "error", str(e))
        return f"Error writing clipboard: {e}"


@app.tool()
async def batch_clipboard_operation(items: List[str], operation: str = "concat") -> str:
    """Batch ops: concat | compare | format | compress | analyze. Result may be written to clipboard."""
    t0 = time.perf_counter()
    if not items:
        return "No items provided."
    cfg = clipboard_server.config
    if not cfg.get("enabled", True):
        return "Clipboard access is disabled."
    ok, msg = ensure_consent()
    if not ok:
        return msg
    try:
        if cfg.get("performance_mode") == "fast" and operation == "concat":
            result = "\n".join(items)
            pyperclip.copy(result)
            dt = time.perf_counter() - t0
            clipboard_server._update_performance(dt, batch=True)
            clipboard_server._audit_log("batch", "success", f"items={len(items)} mode=fast")
            return f"Fast concat items={len(items)} time_ms={dt*1000:.2f}"

        if operation == "concat":
            result = "\n".join(items)
            pyperclip.copy(result)
            msg_out = f"Concatenated {len(items)} items size={len(result)}"
        elif operation == "compare":
            unique = list(set(items))
            dups = len(items) - len(unique)
            msg_out = f"total={len(items)} unique={len(unique)} duplicates={dups}"
        elif operation == "format":
            formatted = [re.sub(r"\s+", " ", i).strip() for i in items]
            result = "\n".join(formatted)
            pyperclip.copy(result)
            msg_out = f"Formatted {len(items)} items size={len(result)}"
        elif operation == "compress":
            seen = set()
            unique_items = []
            for i in items:
                if i not in seen:
                    seen.add(i)
                    unique_items.append(i)
            result = "\n".join(unique_items)
            pyperclip.copy(result)
            msg_out = f"Compressed {len(items)}->{len(unique_items)}"
        elif operation == "analyze":
            avg = sum(len(i) for i in items) // max(1, len(items))
            msg_out = f"items={len(items)} avg_len={avg}"
        else:
            return "Unknown operation. Use concat|compare|format|compress|analyze."

        dt = time.perf_counter() - t0
        clipboard_server._update_performance(dt, batch=True)
        clipboard_server._audit_log("batch", "success", f"op={operation} items={len(items)}")
        return f"{msg_out} time_ms={dt*1000:.2f}"
    except Exception as e:
        clipboard_server._update_performance(time.perf_counter() - t0, batch=True, error=True)
        clipboard_server._audit_log("batch", "error", str(e))
        return f"Batch error: {e}"


@app.tool()
async def get_clipboard_history(last_n: int = 10) -> str:
    """Return recent clipboard history entries (truncated)."""
    if not clipboard_server.config.get("history_enabled", True):
        return "History disabled."
    n = max(1, min(int(last_n), 100))
    hist = clipboard_server.history.get_recent(n)
    if not hist:
        return "(No history)"
    lines = [f"History ({len(hist)}):"]
    for i, e in enumerate(hist, 1):
        c = e["content"]
        if len(c) > 80:
            c = c[:77] + "..."
        lines.append(f"{i}. [{e['type']}] {e['timestamp']}  {c}")
    return "\n".join(lines)


@app.tool()
async def get_performance_stats() -> str:
    """Return clipboard server performance statistics."""
    return clipboard_server.performance.get_report()


@app.tool()
async def clear_sensitive_clipboard() -> str:
    """Clear clipboard if sensitive patterns are detected."""
    try:
        content = pyperclip.paste() or ""
        sensitive, patterns = clipboard_server._detect_sensitive_content(content)
        if sensitive:
            pyperclip.copy("")
            clipboard_server._audit_log("clear_sensitive", "success", f"patterns={len(patterns)}")
            if clipboard_server.config.get("history_enabled"):
                clipboard_server.history.add("[CLEARED SENSITIVE]", "security")
                clipboard_server._save_history()
            return f"Sensitive content cleared (patterns={len(patterns)})."
        clipboard_server._audit_log("clear_sensitive", "success", "clean")
        return "No sensitive content detected."
    except Exception as e:
        clipboard_server._audit_log("clear_sensitive", "error", str(e))
        return f"Error: {e}"


@app.tool()
async def configure_clipboard_server(config_updates: Dict[str, Any]) -> str:
    """Update server configuration keys from DEFAULT_CONFIG."""
    valid = set(DEFAULT_CONFIG.keys())
    bad = set(config_updates.keys()) - valid
    if bad:
        return f"Invalid keys: {sorted(bad)}"
    clipboard_server.config.update(config_updates)
    clipboard_server._save_config()
    clipboard_server._audit_log("config", "success", str(list(config_updates.keys())))
    return "Updated: " + ", ".join(f"{k}={config_updates[k]}" for k in config_updates)


@app.tool()
async def optimize_clipboard_performance() -> str:
    """Apply fast-path performance settings."""
    clipboard_server.config.update(
        {
            "cache_enabled": True,
            "cache_max_entries": 150,
            "batch_mode": True,
            "performance_mode": "fast",
            "history_enabled": True,
            "history_max_entries": 100,
        }
    )
    clipboard_server._save_config()
    clipboard_server._audit_log("optimize", "success", "fast")
    return "Performance mode=fast cache=150 history=100"


@app.tool()
async def security_mode(enabled: bool = True) -> str:
    """Toggle secure performance mode and auto-clear sensitive."""
    clipboard_server.config["performance_mode"] = "secure" if enabled else "balanced"
    clipboard_server.config["auto_clear_sensitive"] = bool(enabled)
    clipboard_server._save_config()
    clipboard_server._audit_log("security", "success", f"enabled={enabled}")
    return f"security_mode={'on' if enabled else 'off'}"


@app.tool()
async def export_clipboard_data() -> str:
    """Export current clipboard metadata and stats as JSON (preview only)."""
    try:
        content = pyperclip.paste() or ""
        h = hashlib.sha256(content.encode("utf-8", errors="replace")).hexdigest()
        data = {
            "current_clipboard": {
                "content_preview": content[:200],
                "size": len(content),
                "hash": h,
                "type": clipboard_server._detect_content_type(content),
                "timestamp": datetime.datetime.now().isoformat(),
            },
            "performance_stats": clipboard_server.performance.stats,
            "history_count": len(clipboard_server.history.entries),
            "cache_count": len(clipboard_server.cache),
        }
        return json.dumps(data, indent=2)
    except Exception as e:
        return f"Error: {e}"


async def _main() -> None:
    if not os.path.exists(CONFIG_FILE):
        clipboard_server._save_config()
    print("Advanced Clipboard MCP Server")
    print(f"  config: {CONFIG_FILE}")
    print(f"  mode:   {clipboard_server.config.get('performance_mode')}")
    print(f"  consent:{clipboard_server.config.get('consent_given')}")
    async with stdio_server() as (read, write):
        await app.run(read, write, app.create_initialization_options())


if __name__ == "__main__":
    asyncio.run(_main())
