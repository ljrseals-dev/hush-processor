# SECURITY HOLD: do not install or run anything in optional/ until these are fixed
Verified by me directly against these files (an uploaded third-party audit raised them first; I re-checked each one).

## tools/server-protection
| # | Finding | Evidence |
|---|---|---|
| 1 | API sends `Access-Control-Allow-Origin: *` with no authentication, CSRF token or Origin check; it runs as root, so any web page you visit could POST to the local API (e.g. the panic/block endpoints) | `server_protection_api.py` lines 354, 372 |
| 2 | Dashboard loads Google Fonts: an external network call every time it is opened | `dashboard/index.html` lines 7-8 |
| 3 | Dashboard shows fabricated data as status: file "hashes" like `a3f9c2d1e4b7...f8c0` and a timer-driven simulation (`tick()` every 100 ms, `setInterval`) | `dashboard/index.html` lines ~511, 758-809 |
| 4 | `install.sh` downloads CSF with `wget` over HTTPS and runs its installer with no checksum or signature check | `install.sh` line 57 |
| 5 | Package installs use `|| true`, so failures are silently ignored | `install.sh` lines ~38-45 |
Not re-checked by me: the blocklist updater's feed-integrity handling (the file layout differs from what the audit described).

## tools/clipboard-mcp
Gives an MCP host read/write access to your clipboard. Installer runs pip, sudo package installs and `claude mcp add`. Not reviewed beyond a pattern scan.

## Rule
Keep both out of the DAW build and installer (they are). If you want them hardened (loopback-only + token auth + same-origin only, local fonts, real data only, checksum-verified downloads, dry-run installer), say so and I will patch copies and test them; I have not modified the originals.
