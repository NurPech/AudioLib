# Changelog
<!--
    Placeholder for the next version (at the beginning of the line):
    ## **WORK IN PROGRESS**
-->

## 0.3.0
- CI now uses the shared `test-changelog`/`sync-public` components from `hannah-components` instead of an inline `sync:public` job — also adds changelog enforcement to this repo's own CI for the first time
- Add stateful streaming variant of `hannah_resample()`: `hannah_resample_ctx_init`/`hannah_resample_ctx` carry the downsample anti-aliasing filter's delay line across consecutive chunk-by-chunk calls, avoiding a settling transient at every chunk boundary when resampling a continuous stream (e.g. a real-time mic pipeline processing fixed-size frames)

## 0.2.3
- Fix `idf_component.yml` version not being bumped by the release script, which made the `publish:registry` CI job fail on release tags (version already on the registry). `scripts/release.js` now updates `idf_component.yml` alongside `CHANGELOG.md`

## 0.2.2
- Publish AudioLib on the [ESP-IDF Component Registry](https://components.espressif.com/components/nurpech/audiolib) (`nurpech/audiolib`), as an MIT-licensed component. Add `idf_component.yml` manifest and root `LICENSE`, an `examples/idf_demo/` project for testing the component standalone, and a CI job to auto-publish new releases

## 0.2.1
- Add public mirror. No functional changes

## 0.2.0
- Add WebRTC VAD backend (`hannah_webrtc_vad_init` / `hannah_webrtc_vad_feed` / `hannah_webrtc_vad_free`), vendored from [libfvad](https://github.com/dpirch/libfvad) under `third_party/libfvad/` (BSD license). Frequency-based, distinguishes speech from music/background noise where the RMS-based VAD can't; runs independently alongside it on a separate state instance.

## 0.1.1
- `hannah_resample`: apply a second-order Butterworth low-pass before downsampling to suppress aliasing (frequencies above the destination Nyquist previously folded back into the output)
- `hannah_resample`: reject negative `in_samples`/`out_samples` (previously only `== 0` was caught)

## 0.1.0
- Initial release of AudioLib
- Batch API: stereo-to-mono downmix, RMS energy, linear-interpolation resampling, voice activity detection (VAD)
- Streaming VAD state machine with debounced onset/offset detection for chunk-by-chunk processing (e.g. I2S DMA buffers)
- ESP-IDF component support via `CMakeLists.txt`
- Unit test suite (`make test`) and GitLab CI pipeline for merge requests