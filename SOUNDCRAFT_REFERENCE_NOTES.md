# SoundCraft (supplied soundcraft-main.zip): reference notes
UPDATE: the automation lane model and memory locations (crates/model/src/automation.rs, markers.rs, about 350 lines) WERE translated to C++; see LANES_AND_MARKERS_NOTES.md. My earlier "months" estimate was for porting the whole 72,000-line project; the other crates are still not ported.
- What it is: a pre-alpha open-source DAW in Rust (about 72,000 lines of .rs), MIT OR Apache-2.0, ArtCraft Team. Zip SHA-256 4fe8506e2529c588ac483a85e62b06be48322d25bfe7b8bad3b0f478b155d3f3.
- Why it is not in this build: spec 1.1 forbids Rust source, Cargo, Rust FFI and Rust runtime dependencies, and a C++20 re-implementation of a project this size is multi-month work.
- Size by crate (lines of Rust): video 16,309; engine 12,941; ui-egui 12,036; dsp 7,137; vst3-host 4,526; clap-host 3,066; model 2,577; audio-io 2,504; mix 2,304; au-host 1,825; midi 1,753; playback 1,176; time 1,111; automation 543.
- Network-capable parts to keep OUT of any port (spec 1.3): crates/automation (MCP server), apps/soundcraft/src/control_server.rs, apps/soundcraft-web (browser/WASM), a git dependency on github.com/storytold/craft-libs, GitHub Actions release workflows.
- Not audited: only the file list, README/NOTICE/ATTRIBUTION, and a pattern search for network crates were read. It has not been built or run, and no security audit was done.
- Suggested port order, if you want to continue (each needs an approved C++ dependency decision): midi (1.7k) -> mix (2.3k) -> playback (1.2k, needs a device backend) -> engine commands/undo -> dsp plugins -> a GUI toolkit -> plugin hosting (VST3/CLAP SDKs).
- fk::time in this package was written from scratch against the spec, not translated from crates/time.
