# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

AudioLib (`libhannah_audio`) is a small C library for lightweight PCM audio processing on embedded targets (ESP32/ESP-IDF), also buildable as a plain C library on desktop/CI. It provides building blocks for voice-assistant audio pipelines: format conversion, level metering, resampling, and voice activity detection (VAD) — as one-shot batch operations and as a streaming state machine for chunk-by-chunk processing (e.g. from an I2S DMA buffer). It's part of the "Hannah" voice-assistant project and is consumed by exactly one downstream project (Hannah itself).

The full public API with detailed docs lives in [include/libhannah_audio.h](include/libhannah_audio.h) — read it before changing behavior.

## Commands

```sh
make            # builds lib/libhannah_audio.a and bin/test (examples/main.c)
./bin/test
make test       # builds bin/run_tests from tests/test_hannah_audio.c and runs it
make clean
```

There is no test filtering flag — `tests/test_hannah_audio.c` is a single assert-based suite (`static void test_*` functions called from `main`); to run a subset temporarily, comment out calls in its `main()`.

CI (`.gitlab-ci.yml`) runs `make test` (image `gcc:16`) on every merge request.

### ESP-IDF component build

The root `CMakeLists.txt` registers the library as a plain ESP-IDF component (`idf_component_register`) — it does **not** call `project()` and cannot be built with `idf.py` directly. To build/test it as an ESP-IDF component, use `examples/idf_demo/`, a standalone ESP-IDF project that pulls in AudioLib via a local `path` dependency in `examples/idf_demo/main/idf_component.yml`:

```sh
cd examples/idf_demo
idf.py build
```

Do not reintroduce a `project()`/self-bootstrap hack in the root `CMakeLists.txt` to make `idf.py build` work there directly — that was tried and reverted because it pollutes the CMakeLists every consumer sees. Test via `examples/idf_demo/` instead.

## Architecture

- **`include/libhannah_audio.h`** — the entire public API surface, split into three sections: batch API (`hannah_stereo_to_mono`, `hannah_rms`, `hannah_resample`, `hannah_vad`), streaming RMS-based VAD (`hannah_vad_stream_init`/`hannah_vad_feed`), and streaming WebRTC-based VAD (`hannah_webrtc_vad_init`/`hannah_webrtc_vad_feed`/`hannah_webrtc_vad_free`).
- **`src/hannah_audio.c`** — batch API + RMS-based streaming VAD. `hannah_resample` applies a second-order Butterworth low-pass in-place before downsampling to suppress aliasing.
- **`src/hannah_webrtc_vad.c`** — thin wrapper around vendored libfvad, adapting it to the same debounced `ONSET`/`SPEECH`/`OFFSET`/`SILENCE` state machine shape as the RMS-based streaming VAD, so both backends are drop-in interchangeable from the caller's perspective. Requires fixed 10/20/30ms frames at 8/16/32/48kHz. The two VAD backends run on independent state instances and are meant to be usable side by side (e.g. RMS for a cheap wakeword-guard threshold, WebRTC VAD for accurate end-of-speech detection).
- **`third_party/libfvad/`** — vendored, BSD-licensed (see `third_party/libfvad/LICENSE`), not modified in place; don't hand-edit vendored sources, update by re-vendoring from upstream.
- **`examples/main.c`** — desktop example (sine wave → resample → VAD), built by the plain Makefile.
- **`examples/idf_demo/`** — standalone ESP-IDF example project, the sanctioned way to build/test AudioLib as an actual ESP-IDF component (see above).
- **`tests/test_hannah_audio.c`** — the only test file; assert-based, no framework.

### Release / publishing flow

- Versioning follows `CHANGELOG.md`, with unreleased changes collected under a `## **WORK IN PROGRESS**` marker at the top.
- `scripts/release.js <major|minor|patch> [--dry-run] [--yes]` automates a release: moves the WIP changelog section into a new version heading, bumps the `version:` field in `idf_component.yml` to match, commits both as `chore: bump to version X.Y.Z` on a `release/vX.Y.Z` branch, opens a GitLab MR, waits for the pipeline, enables merge-when-pipeline-succeeds, waits for the merge, then tags and pushes `vX.Y.Z`. Requires `GITLAB_TOKEN` (in `.env` or env) and must be run from `main`/`master`. When adding new release-time bookkeeping files (anything else that embeds a version number), extend `release.js` to bump them too — it's the single source of truth for what a release touches, and CI failures from a stale version field elsewhere (see the `idf_component.yml`/registry incident) are easy to miss until a tagged pipeline fails.
- On every release tag, GitLab CI (`.gitlab-ci.yml`) runs two independent jobs in the `sync` stage: `sync:public` (mirrors the release to the public `github.com/NurPech/AudioLib`) and `publish:registry` (uploads the component to the ESP-IDF Component Registry as `nurpech/audiolib` via `idf_component_manager`). `publish:registry` fails hard if `idf_component.yml`'s `version` doesn't match the new tag and doesn't retry — `release.js` keeping it in sync is what prevents that.
- AudioLib is published at https://components.espressif.com/components/nurpech/audiolib.
