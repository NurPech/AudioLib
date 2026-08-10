#ifndef LIBHANNAH_AUDIO_H
#define LIBHANNAH_AUDIO_H

#include <stddef.h>
#include <stdint.h>

/* ── Batch API ─────────────────────────────────────────────────────────────
 * Process a complete, already-captured buffer.                             */

/* Convert interleaved stereo PCM to mono by averaging both channels.
 * in:          interleaved stereo buffer [L, R, L, R, ...], length = frames * 2
 * out:         mono output buffer, length = frames
 * frames:      number of stereo frames (= total samples / 2)
 * Returns 0 on success, -1 on bad arguments. */
int hannah_stereo_to_mono(const int16_t *in, int16_t *out, int frames);

/* RMS energy of pcm[0..samples-1], normalised to [0.0, 1.0]. */
float hannah_rms(const int16_t *pcm, int samples);

/* Resample in[0..in_samples-1] from src_rate to dst_rate using linear
 * interpolation.  out must have room for out_samples samples, where
 * out_samples == (int)(in_samples * dst_rate / src_rate + 0.5).
 * When downsampling (dst_rate < src_rate) a second-order Butterworth low-pass
 * is applied first to suppress aliasing; this runs in-place with no extra
 * allocation but does shift/attenuate content near the destination Nyquist.
 * Returns 0 on success, -1 on bad arguments. */
int hannah_resample(const int16_t *in,  int in_samples,  int src_rate,
                          int16_t *out, int out_samples, int dst_rate);

/* Returns 1 if any window in pcm exceeds threshold, else 0.
 * window_size: samples per analysis window (e.g. 512 @ 16kHz = 32ms).
 * threshold:   normalised energy in [0.0, 1.0] (try 0.02–0.1). */
int hannah_vad(const int16_t *pcm, int samples, int window_size, float threshold);


/* ── Streaming resample ───────────────────────────────────────────────────
 * Feed audio chunk-by-chunk (e.g. from an I2S DMA buffer) through the same
 * resampling algorithm as hannah_resample(), while carrying the downsample
 * anti-aliasing filter's delay-line state across calls. Calling this
 * repeatedly on consecutive, contiguous chunks of a stream (same ctx, same
 * src_rate/dst_rate each call) is equivalent to calling hannah_resample()
 * once on the whole concatenated buffer: a single settling transient at
 * stream start instead of one per chunk. Use hannah_resample() instead for
 * one-shot conversion of a complete buffer.
 *
 * Typical flow:
 *   hannah_resample_ctx_t ctx;
 *   hannah_resample_ctx_init(&ctx);
 *   while (i2s_read(in, 480) == OK) {
 *       hannah_resample_ctx(&ctx, in, 480, 48000, out, 160, 16000);
 *       // ... use out ...
 *   }                                                                       */

typedef struct {
    double b0, b1, b2, a1, a2;  /* internal: anti-aliasing LPF coefficients */
    double z1, z2;              /* internal: anti-aliasing LPF delay line   */
    int    filtering;           /* internal: was the last call downsampling */
    int    src_rate;            /* internal: rates the coefficients above   */
    int    dst_rate;            /*           were computed for              */
} hannah_resample_ctx_t;

/* Initialise the context. Call once before the first hannah_resample_ctx(). */
void hannah_resample_ctx_init(hannah_resample_ctx_t *ctx);

/* Stateful counterpart to hannah_resample() — see arguments and the
 * out_samples sizing rule there, which apply unchanged per call. When
 * downsampling, the anti-aliasing filter's delay line carries over from the
 * previous call on this ctx, as long as src_rate/dst_rate stay the same; a
 * change in either restarts filtering with a fresh delay line.
 * Returns 0 on success, -1 on bad arguments (including ctx == NULL). */
int hannah_resample_ctx(hannah_resample_ctx_t *ctx,
                         const int16_t *in,  int in_samples,  int src_rate,
                         int16_t *out, int out_samples, int dst_rate);


/* ── Streaming VAD ─────────────────────────────────────────────────────────
 * Feed audio chunk-by-chunk (e.g. from an I2S DMA buffer).
 * The state machine debounces onset and offset so brief noise or brief
 * pauses don't flip the result on every chunk.
 *
 * Typical flow on ESP32:
 *   hannah_vad_state_t vad;
 *   hannah_vad_stream_init(&vad, 3, 8, 0.05f);   // init once
 *   while (i2s_read(buf, N) == OK) {
 *       int evt = hannah_vad_feed(&vad, buf, N);
 *       if (evt == HANNAH_VAD_ONSET)   // speech just started → begin recording
 *       if (evt == HANNAH_VAD_OFFSET)  // speech just ended   → send to STT
 *   }                                                                       */

#define HANNAH_VAD_SILENCE  0   /* still / continuously silent              */
#define HANNAH_VAD_ONSET    1   /* speech started  (transition: off → on)   */
#define HANNAH_VAD_SPEECH   2   /* speech ongoing  (no state change)        */
#define HANNAH_VAD_OFFSET   3   /* speech ended    (transition: on → off)   */

typedef struct {
    float threshold;        /* normalised energy threshold, e.g. 0.05      */
    int   onset_windows;    /* consecutive active windows to confirm onset  */
    int   offset_windows;   /* consecutive silent windows to confirm offset */
    int   speaking;         /* internal: currently in speech?              */
    int   onset_count;      /* internal: active-window counter             */
    int   offset_count;     /* internal: silence-window counter            */
} hannah_vad_state_t;

/* Initialise the state.  Call once before the first hannah_vad_feed().
 *   onset_windows  – how many consecutive active chunks trigger ONSET
 *                    (higher = less sensitive to short noise, try 3)
 *   offset_windows – how many consecutive silent chunks trigger OFFSET
 *                    (higher = tolerates short pauses in speech, try 8)
 *   threshold      – normalised energy, same scale as hannah_rms() */
void hannah_vad_stream_init(hannah_vad_state_t *state,
                            int   onset_windows,
                            int   offset_windows,
                            float threshold);

/* Feed one chunk of audio.  chunk_size is the number of samples in buf.
 * Returns one of the HANNAH_VAD_* constants above. */
int hannah_vad_feed(hannah_vad_state_t *state,
                    const int16_t *buf, int chunk_size);


/* ── Streaming VAD: WebRTC backend ───────────────────────────────────────
 * Frequency-based VAD (vendored libfvad / WebRTC's VAD core, see
 * third_party/libfvad/), distinguishing speech from music/background noise
 * far better than the RMS-threshold backend above, at the cost of a fixed
 * frame-size constraint (see hannah_webrtc_vad_feed).  Same debounced
 * ONSET/SPEECH/OFFSET/SILENCE state machine as hannah_vad_feed(); the two
 * backends are independent and can run side by side on separate state
 * instances — e.g. RMS for a wakeword-guard threshold, WebRTC VAD for
 * end-of-speech detection during an active stream. */

typedef struct {
    void *vad_inst;          /* internal: opaque vendored Fvad* instance     */
    int   sample_rate;       /* 8000, 16000, 32000 or 48000                  */
    int   onset_windows;     /* consecutive active frames to confirm onset   */
    int   offset_windows;    /* consecutive silent frames to confirm offset  */
    int   speaking;          /* internal: currently in speech?               */
    int   onset_count;       /* internal: active-frame counter               */
    int   offset_count;      /* internal: silence-frame counter              */
} hannah_webrtc_vad_state_t;

/* Initialise the state and allocate the underlying VAD instance.
 *   aggressiveness – 0 (quality, most sensitive to speech) .. 3 (very
 *                    aggressive against non-speech)
 *   sample_rate    – 8000, 16000, 32000 or 48000
 *   onset_windows / offset_windows – see hannah_vad_stream_init()
 * Returns 0 on success, -1 on invalid aggressiveness/sample_rate or
 * allocation failure.  Call hannah_webrtc_vad_free() when done. */
int hannah_webrtc_vad_init(hannah_webrtc_vad_state_t *state,
                            int aggressiveness, int sample_rate,
                            int onset_windows, int offset_windows);

/* Feed one frame of audio.  chunk_size must be exactly 10, 20 or 30 ms worth
 * of samples at the sample_rate passed to hannah_webrtc_vad_init() (e.g.
 * 160/320/480 samples at 16kHz).  The caller is responsible for providing
 * correctly-sized frames; this is not validated against the stream's actual
 * sample rate, only against the configured one.
 * Returns a HANNAH_VAD_* constant, or -1 if chunk_size doesn't match an
 * allowed frame length for the configured sample_rate. */
int hannah_webrtc_vad_feed(hannah_webrtc_vad_state_t *state,
                            const int16_t *buf, int chunk_size);

/* Releases the underlying VAD instance allocated by hannah_webrtc_vad_init(). */
void hannah_webrtc_vad_free(hannah_webrtc_vad_state_t *state);

#endif /* LIBHANNAH_AUDIO_H */
