# Advanced Clipboard MCP Server

Optional MCP companion under DawChecker `tools/clipboard-mcp`.

**Not** part of the offline audio/GUI binary. Separate stdio process for AI hosts.

## Install (venv)

```bash
cd tools/clipboard-mcp
./install_clipboard_server.sh
```

Creates `.venv/`, installs `mcp[cli]` + `pyperclip`, optional `xclip`/`python3-tk`,
writes `clipboard_advanced_config.json`, and rewrites `start_clipboard_server.sh`.

## Run

```bash
./start_clipboard_server.sh
```

## Claude Code

```bash
claude mcp add clipboard-advanced -- \
  "$(pwd)/.venv/bin/python" \
  "$(pwd)/clipboard_server_advanced.py"
```

## Tools

| Tool | Purpose |
|------|---------|
| `read_clipboard_fast` | Read + type + cache |
| `write_clipboard_fast` | Write |
| `batch_clipboard_operation` | concat / compare / format / compress / analyze |
| `get_clipboard_history` | Recent items |
| `get_performance_stats` | Timing / hits |
| `clear_sensitive_clipboard` | Clear if sensitive |
| `configure_clipboard_server` | Live config |
| `optimize_clipboard_performance` | Fast presets |
| `security_mode` | Secure vs balanced |
| `export_clipboard_data` | JSON metadata |

## Privacy

Consent defaults **off**. Clipboard may be sent to the MCP host AI when tools run.
