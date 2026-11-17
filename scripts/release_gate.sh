#!/usr/bin/env bash
# HUSH release gate: reproducible build/tests plus explicit manual gates.
# Usage: scripts/release_gate.sh [build-dir]
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT}/build-release-gate}"
EVIDENCE_DIR="${ROOT}/evidence/release_gate"
mkdir -p "$EVIDENCE_DIR"
LOG="$EVIDENCE_DIR/run.log"
exec > >(tee "$LOG") 2>&1

fail() { echo "GATE_FAIL: $*"; exit 1; }
command -v cmake >/dev/null || fail "cmake not found"
command -v ctest >/dev/null || fail "ctest not found"
command -v sha256sum >/dev/null || fail "sha256sum not found"

printf 'HUSH release gate\nUTC: %s\nSource: %s\n' "$(date -u +%FT%TZ)" "$ROOT"
cmake --version | head -n1
if command -v c++ >/dev/null; then c++ --version | head -n1; fi

# Offline-only configure: no dependency-fetch step is part of this gate.
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DHUSH_WARNINGS_AS_ERRORS=ON \
  -DHUSH_ENABLE_PORTAUDIO=ON \
  -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --parallel
# The detector calibration corpus is intentionally excluded from this fast release gate:
# it is a long-running calibration job and must be run separately before a full sign-off.
ctest --test-dir "$BUILD_DIR" --output-on-failure -E hush_detector_calibration

if [[ -x "$BUILD_DIR/hush" ]]; then
  "$BUILD_DIR/hush" help > "$EVIDENCE_DIR/cli-help.txt"
  grep -q 'session-split-clip' "$EVIDENCE_DIR/cli-help.txt" || fail "CLI help missing session-split-clip"
else
  fail "hush CLI binary missing"
fi

# Check source manifest, excluding the manifest itself (manifest may predate this gate file).
if [[ -f "$ROOT/SOURCE_SHA256SUMS.txt" ]]; then
  (cd "$ROOT" && sha256sum -c SOURCE_SHA256SUMS.txt) > "$EVIDENCE_DIR/source-checksums.txt" || fail "source checksum validation failed"
fi

cat <<'REPORT'

AUTOMATED GATE: PASS
This means configure/build, CTest, CLI smoke included by CTest, and available source checksums passed.
It does NOT mean the product is ready for production release. Detector calibration is excluded from this fast gate and must be run separately.

MANUAL / DEVICE GATES (must be signed off separately):
[ ] Physical audio output tested on named device, sample rate, and buffer size.
[ ] Physical audio input capture tested; output WAV reopened and inspected.
[ ] Record/stop behavior tested for errors, short takes, long takes, and device loss.
[ ] Input monitoring latency and feedback behavior tested (feature currently not claimed complete).
[ ] Track arm, punch-in/out, pre/post-roll and loop-take playlist tested (not claimed complete).
[ ] Autosave and crash recovery tested (not claimed complete).
[ ] Release artifact reviewed, signed, and independently security-reviewed as required.

FINAL RELEASE DECISION: HOLD until every applicable manual gate is completed and remaining
P1 requirements are implemented. Do not interpret automated PASS as production approval.
REPORT
