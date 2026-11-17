# Optional standalone tools (NOT part of the FortKnox build, installer, or runtime)
Unmodified copies from the DawChecker archive. Nothing here is compiled, installed, started, or registered by FortKnox.
- clipboard-mcp/: Python MCP server. Reads/writes the system clipboard and exposes it to the configured MCP host. Installer runs pip, sudo package installs, and `claude mcp add`. Launch manually only.
- server-protection/: Linux admin toolkit. Needs root; downloads CSF; edits firewall config; adds cron + systemd; fetches external blocklists. Do NOT run install.sh without reading it, a dry run, and backups (firewall mistakes can lock you out of SSH). Per spec 12.3 treat as source/docs only until its safety properties are verified.
Only a static pattern scan has been done on these files (see ../SECURITY_AND_PRIVACY.md).
