# AudioLib

A small C library for lightweight PCM audio processing, written for
embedded targets (ESP32 / ESP-IDF) and usable as a plain C library on
desktop/CI for testing.

It provides simple building blocks for voice-assistant style audio
pipelines: format conversion, level metering, resampling, and voice
activity detection (VAD) — both as one-shot batch operations and as a
streaming state machine for chunk-by-chunk processing (e.g. from an
I2S DMA buffer).

## Features

- **`hannah_stereo_to_mono`** – convert interleaved stereo PCM to mono
  by averaging both channels.
- **`hannah_rms`** – RMS energy of a PCM buffer, normalised to `[0.0, 1.0]`.
- **`hannah_resample`** – resample PCM between sample rates using linear
  interpolation.
- **`hannah_vad`** – batch voice activity detection over a complete buffer.
- **Streaming VAD** (`hannah_vad_stream_init` / `hannah_vad_feed`) – a
  debounced state machine that reports `ONSET`/`SPEECH`/`OFFSET`/`SILENCE`
  events as audio is fed in chunk-by-chunk, so brief noise or pauses don't
  flip the result on every chunk.
- **Streaming VAD, WebRTC backend** (`hannah_webrtc_vad_init` /
  `hannah_webrtc_vad_feed` / `hannah_webrtc_vad_free`) – same debounced event
  state machine, but backed by [libfvad](https://github.com/dpirch/libfvad)
  (vendored under `third_party/libfvad/`, BSD-licensed), a frequency-based
  VAD that tells speech apart from music/background noise far better than an
  RMS threshold. Requires fixed 10/20/30ms frames at 8/16/32/48kHz. Runs
  independently of the RMS-based VAD, so both can be used side by side.

See [include/libhannah_audio.h](include/libhannah_audio.h) for the full API
with detailed documentation.

## Usage

### As an ESP-IDF component

Add this repository as a component (e.g. via git submodule under
`components/`). The provided `CMakeLists.txt` registers the library with
`idf_component_register`.

### As a plain C library

```sh
make            # builds lib/libhannah_audio.a and bin/test (example)
./bin/test
```

```c
#include "libhannah_audio.h"

int16_t mono[NUM_FRAMES];
hannah_stereo_to_mono(stereo_in, mono, NUM_FRAMES);

float level = hannah_rms(mono, NUM_FRAMES);

if (hannah_vad(mono, NUM_FRAMES, 512, 0.05f)) {
    // voice detected
}
```

See [examples/main.c](examples/main.c) for a complete example (sine wave
generation, resampling, VAD).

## Testing

A small assert-based test suite covers the core functions:

```sh
make test
```

This builds `bin/run_tests` and runs it. The GitLab CI pipeline
(`.gitlab-ci.yml`) runs `make test` for every merge request.
