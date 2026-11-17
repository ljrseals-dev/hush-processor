# DawChecker archive: source/dependency inventory and static audit (Spec §9, step 1)

**Archive:** `DawChecker-Secure-Ultimate-with-ServerProtection-1.zip`
**SHA-256:** `9dc185fe4238f93119c48ff9e30985b1a468a9df737e4ce2ab31f9067f7f3735`
**Size / contents:** 117,551 bytes, 74 entries (about 50 files), extracted to a scratch directory only
**License:** MIT, Copyright (c) 2026 Lewis James Seals Jr (preserve the notice)
**Method:** static pattern search (network, process/exec, dynamic loading, URLs, persistence) plus targeted reading of key files. Nothing was built or executed.

## Scope of what was and was not inspected

| Inspected | Not inspected / not verified |
|---|---|
| Full file listing; non-source/binary file search (none found) | Line-by-line review of every C++ file (grep plus targeted reads only) |
| Pattern grep over `src/`, `tests/`, `Source/`, `latest/`, `CMakeLists.txt` | `tools/server-protection/dashboard/index.html` (60 KB), not read for external scripts/fonts |
| `Neutralizers.h`, `SecurityHardening.*`, `WatermarkDetector.h`, `DetectorTypes.h` | `server_protection_api.py` and the rest of the Python body, only pattern-scanned |
| Pattern scan of `tools/clipboard-mcp/*` and `tools/server-protection/*` | Build, unit tests, runtime behavior, network-isolated testing: **none performed** |
| `Source/` vs `latest/` diff | Algorithm validity (ENF, MSV, ultrasonic, watermark): not validated |

Static search alone does not prove the absence of a backdoor.

## Findings: C++ core (`src/core`, `tests`)

- No hits for sockets, curl/wget, `system`/`popen`/`exec`, `fork`, `dlopen`/`LoadLibrary`, `ShellExecute`, `getaddrinfo`, or `FetchContent` in the core.
- `SecurityHardening.cpp` performs read-only local file reads: `/proc/self/status` (TracerPid), `/proc/sys/kernel/randomize_va_space`, and an optional `<exe>.sha256` companion. These need a final read before adoption, but they are informational and local.
- Neutralizers (`Neutralizers.h`) are signal-modifying DSP:
  - `ENFNotchFilter` (cascaded notches)
  - `UltrasonicRolloff` (lowpass at 17 kHz by default)
  - `WatermarkScrubber` (subtracts the estimated watermark bias from live audio, scaled by detector confidence)
  - **Flag:** the spec forbids automatic neutralization and requires non-destructive, explicit, previewed actions. `WatermarkScrubber` is in effect watermark *removal*, which the spec does not address. Proposed default: stub all three behind an explicit offline-render action, and keep `WatermarkScrubber` disabled pending your decision.
- `WatermarkDetector` is detection-only (live vs reference residual). Per spec, no embedder exists, so Watermark Render stays "not implemented".
- Tests: `tests/EngineTests.cpp` has a single `main()` and is registered with CTest. Its coverage was not evaluated.

## Findings: GUI (`src/gui`)

- `Application.cpp:20` `kReleasesUrl = https://github.com/TheRealla/DawChecker/releases` and `:200` `ImGui::TextLinkOpenURL("Get Latest Release", …)`. **Remove per §2.1.**
- The GUI depends on Dear ImGui, which is **absent from the archive** (`third_party/imgui` missing). `scripts/vendor_imgui.sh` downloads it, and `CMakeLists.txt:139` prints a `git clone` hint. **Missing dependency, approval required** (§1.1, §4). Also needs GLFW and OpenGL from the system.

## Findings: build (`CMakeLists.txt`, `scripts/`)

- No `FetchContent`. A custom target "Vendoring Dear ImGui" (around line 277) calls the download script. **Remove from the default build.**
- `DAWCHECKER_USE_PORTAUDIO` defaults **ON** (live audio input). The ALSA path is also linked on Linux. Confirm that capture starts only on an explicit user action before enabling by default.
- `scripts/build_hardened.sh` and `build_debug_sanitizers.sh`: not yet reviewed.

## Findings: `Source/` and `latest/` (VST3 wrapper)

- Both are single-file VST3 plugins (`dawchecker_vst3.cpp`) needing the Steinberg SDK, which is **not in the archive**. The only URL is the vendor-info string `https://github.com/TheRealla` (metadata, not a network call).
- `latest/` differs from `Source/` by fixes: a missing `pluginfactory.h` include, missing `write_pos_`/`read_pos_` members, a nonexistent `setControllerClass()` call, and an invalid `kDistortion` constant. `latest/` is the more plausible variant if a VST3 build is ever approved.
- **Proposed canonical choice for HUSH:** `src/core` (shared C++ analysis code). Exclude both VST3 variants from the default build, since the SDK is missing and a plugin wrapper is not needed inside a DAW. Document this decision.

## Findings: `tools/clipboard-mcp` (Python, separate)

- Depends on `pyperclip` and `mcp[cli]`. It reads and writes the system clipboard (`pyperclip.paste`/`copy`, with clear and transform operations), so contents can reach the MCP host.
- The installer runs `pip install`, uses `sudo apt-get/yum/pacman` for xclip/xsel/tk, and registers itself via `claude mcp add`.
- **Exclude from the default build and package** (§2.1, §11). A consent-gated C++ clipboard checker would be a new, separate implementation.

## Findings: `tools/server-protection` (Linux admin toolkit, separate)

- `install.sh`: needs root; `apt-get/yum` installs (curl, wget, perl, ipset, clamav, rkhunter); `wget`s CSF from `download.configserver.com` and runs its installer; edits `/etc/csf/csf.conf`; adds a **crontab** entry; enables and starts a **systemd** service.
- `msv-blocklist-update.sh`: root, modifies `/etc/csf/csf.blocklists`, fetches external blocklists.
- Dashboard/API referenced at `127.0.0.1:8080`. Authentication/CSRF posture not yet reviewed.
- **Exclude entirely from the DAW** (§2.1, §12). If retained, ship as source/docs only.

## Dependency inventory (draft)

| Component | Origin | Version | License | Status |
|---|---|---|---|---|
| DawChecker core (`src/core`) | user archive | n/a (no tag) | MIT | candidate; review pending |
| DawChecker GUI (`src/gui`) | user archive | n/a | MIT | exclude from default until native GUI choice is approved |
| Dear ImGui | not supplied (download script) | v1.90.9 per CMake hint | MIT (per upstream) | **missing, needs approval** |
| GLFW / OpenGL | system | unknown | zlib / n/a | unapproved |
| PortAudio / ALSA | system | unknown | MIT-like / LGPL | unapproved |
| Steinberg VST3 SDK | not supplied | n/a | n/a | excluded |
| pyperclip, mcp[cli] | pip (tools/clipboard-mcp) | n/a | n/a | excluded |
| CSF, ipset, ClamAV, rkhunter | system/download (tools/server-protection) | n/a | n/a | excluded |

## Decisions I need from you

1. **Neutralizers:** keep as explicit offline render actions, or leave stubbed? Is `WatermarkScrubber` (watermark removal) in scope at all?
2. **GUI:** approve Dear ImGui v1.90.9 (you supply the source, no download), or choose another native C++ GUI?
3. **Audio backend:** which platform first (WASAPI, CoreAudio, or ALSA)? PortAudio only if you approve it.
4. **Source tree:** the HUSH/SoundCraft DAW source is still needed for any real merge. The archive contains only DawChecker.

## Next steps (spec §9, steps 2–4)

Create the offline CMake skeleton with `fk::time` and tests, plus the `README`, `PORTING_STATUS.md`, `SECURITY_AND_PRIVACY.md` and `DEPENDENCIES.md` templates. `PORTING_STATUS.md` will mark everything as not implemented except what is actually built and tested.


## HUSH 2.1.0 review additions
- New active code: `src/audio_engine.*`, `src/portaudio_backend.*`, `src/midi.*`, `src/plugin_host.*`, and `src/gui_x11.cpp`. The PortAudio backend is optional and links only a system library found by CMake; no binary plugin loading or network client was added.
- WAV output and session save use temporary-file atomic replacement/create on POSIX; symlink targets are rejected. Session loading has a 4 MiB cap and rejects symlink inputs.
- `scripts/audit_source.sh` now scopes its default scan to active C++ source, the hardened DawChecker fork and CMake; optional admin/clipboard tools remain separate and need their own audit.
- This is a source-level review update, not a full line-by-line audit or security certification.
