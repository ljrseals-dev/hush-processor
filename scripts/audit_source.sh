#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=${1:-"$SCRIPT_DIR/.."}
cd "$ROOT"
CORE_PATHS="src CMakeLists.txt third_party/dawchecker_hardened/src"
PATTERN='curl|wget|FetchContent|Cargo\.toml|Cargo\.lock|system\(|popen\(|CreateProcess|execve|fork\(|socket\(|connect\(|getaddrinfo|WinHttp|URLSession|telemetry|analytics|crash.?report|update.?check'
echo "HUSH active-code static audit: $ROOT"
echo "Scope: active C++ source, hardened DawChecker source and CMake. Optional tools are not part of the DAW build."
echo "NOTE: text search is advisory, not a definitive security audit. Review every hit manually."
if command -v rg >/dev/null 2>&1; then
  # Limit by file type so documentation mentions do not drown source findings.
  rg -n -i "$PATTERN" $CORE_PATHS --glob '*.cpp' --glob '*.hpp' --glob '*.h' --glob '*.c' --glob '*.cc' --glob 'CMakeLists.txt' || true
else
  grep -RniE "$PATTERN" src third_party/dawchecker_hardened/src CMakeLists.txt --include='*.cpp' --include='*.hpp' --include='*.h' --include='*.c' --include='*.cc' --include='CMakeLists.txt' || true
fi
echo "Separate optional-tool review required: optional/clipboard-mcp and optional/server-protection."
echo "Search complete; this is not a security certification."
