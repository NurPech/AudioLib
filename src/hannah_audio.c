#include "libhannah_audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#   define M_PI 3.14159265358979323846
#endif

/* ── Internal helpers ───────────────────────────────────────────────────── */

float hannah_rms(const int16_t *pcm, int samples) {
    if (!pcm || samples == 0) {
        return 0.0f;
    }
    int64_t sum_squares = 0;
    for (int i = 0; i < samples; ++i) {
        int64_t curr = pcm[i];
        sum_squares += curr * curr;
    }
    return (float)sqrt((double)sum_squares / samples) / 32767.0f;
}

static float hannah_peak_abs(const int16_t *samples, size_t count) {
    if (!samples || count == 0) {
        return 0.0f;
    }
    int max_value = abs(samples[0]);
    for (size_t i = 1; i < count; ++i) {
        int curr = abs(samples[i]);
        if (curr > max_value) {
            max_value = curr;
        }
    }
    return (float)max_value / 32767.0f;
}

/* ── Anti-aliasing low-pass (biquad, RBJ cookbook) ──────────────────────────
 * A second-order Butterworth low-pass used before downsampling so content
 * above the destination Nyquist doesn't fold back as aliasing.  Transposed
 * direct form II; processed in double, one sample at a time, so no buffer is
 * required.  Note the RBJ low-pass has a zero at fs/2, fully rejecting the
 * source Nyquist. */
typedef struct {
    double b0, b1, b2, a1, a2;   /* coefficients, normalised so a0 == 1 */
    double z1, z2;               /* filter state                       */
} hannah_biquad_t;

static void biquad_lowpass_init(hannah_biquad_t *bq, double cutoff_hz, double sample_rate) {
    const double q = 0.70710678;  /* Butterworth (maximally flat) */
    double w0     = 2.0 * M_PI * cutoff_hz / sample_rate;
    double cos_w0 = cos(w0);
    double sin_w0 = sin(w0);
    double alpha  = sin_w0 / (2.0 * q);

    double b0 = (1.0 - cos_w0) / 2.0;
    double b1 =  1.0 - cos_w0;
    double b2 = (1.0 - cos_w0) / 2.0;
    double a0 =  1.0 + alpha;
    double a1 = -2.0 * cos_w0;
    double a2 =  1.0 - alpha;

    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
    bq->z1 = 0.0;
    bq->z2 = 0.0;
}

static double biquad_process(hannah_biquad_t *bq, double x) {
    double y = bq->b0 * x + bq->z1;
    bq->z1   = bq->b1 * x - bq->a1 * y + bq->z2;
    bq->z2   = bq->b2 * x - bq->a2 * y;
    return y;
}

/* Returns 1 if the window is considered active (voice or loud noise). */
static int window_active(const int16_t *samples, size_t count, float threshold) {
    if (!samples || count == 0) {
        return 0;
    }
    float rms  = hannah_rms(samples, count);
    float peak = hannah_peak_abs(samples, count);
    return (rms > threshold || peak > threshold) ? 1 : 0;
}

/* ── Batch API ───────────────────────────────────────────────────────────── */

int hannah_stereo_to_mono(const int16_t *in, int16_t *out, int frames) {
    if (!in || !out || frames <= 0) {
        return -1;
    }
    for (int i = 0; i < frames; ++i) {
        int32_t mixed = (int32_t)in[i * 2] + (int32_t)in[i * 2 + 1];
        out[i] = (int16_t)(mixed / 2);
    }
    return 0;
}

/* Shared resampling loop behind hannah_resample() and hannah_resample_ctx().
 * bq must already carry the right coefficients for (src_rate, dst_rate) and
 * whatever delay-line state the caller wants this call to continue from;
 * its z1/z2 are left holding the state as of the last output sample, so a
 * caller that wants continuity across calls just keeps reusing the same
 * hannah_biquad_t. Only read when dst_rate < src_rate. */
static int hannah_resample_core(const int16_t *in,  int in_samples,  int src_rate,
                                 int16_t *out, int out_samples, int dst_rate,
                                 hannah_biquad_t *bq) {
    if (!in || in_samples <= 0 || src_rate <= 0 || !out || out_samples <= 0 || dst_rate <= 0) {
        return -1;
    }
    if (out_samples != (int)((double)in_samples * dst_rate / src_rate + 0.5)) {
        return -1;
    }
    double ratio = (double)dst_rate / src_rate;
    int    filtering = (dst_rate < src_rate);

    int    filt_pos  = -1;     /* highest source index already filtered */
    double filt_prev = 0.0;    /* filtered sample at filt_pos - 1       */
    double filt_curr = 0.0;    /* filtered sample at filt_pos           */

    for (int i = 0; i < out_samples; ++i) {
        double src_index  = i / ratio;
        int    idx_floor  = (int)floor(src_index);
        int    idx_ceil   = (int)ceil(src_index);
        if (idx_ceil  >= in_samples) idx_ceil  = in_samples - 1;
        if (idx_floor >= in_samples) idx_floor = in_samples - 1;

        double s_floor, s_ceil;
        if (filtering) {
            /* Advance the filter up to idx_ceil; idx_floor is the sample
             * right before it (or the same one at the boundary). */
            while (filt_pos < idx_ceil) {
                filt_prev = filt_curr;
                filt_curr = biquad_process(bq, (double)in[filt_pos + 1]);
                ++filt_pos;
            }
            s_ceil  = filt_curr;
            s_floor = (idx_floor == idx_ceil) ? filt_curr : filt_prev;
        } else {
            s_floor = (double)in[idx_floor];
            s_ceil  = (double)in[idx_ceil];
        }

        double w_ceil  = src_index - idx_floor;
        double w_floor = 1.0 - w_ceil;
        out[i] = (int16_t)(s_floor * w_floor + s_ceil * w_ceil);
    }

    if (filtering) {
        /* The loop above only walks the filter up to the last source index
         * an output sample actually needs, which for a one-shot call is
         * every trailing sample that's simply never read. But a chunked
         * caller's next call picks up right where this input left off, so
         * those trailing samples still need to pass through the filter now
         * to leave bq's delay line correct for the next chunk. */
        while (filt_pos < in_samples - 1) {
            filt_prev = filt_curr;
            filt_curr = biquad_process(bq, (double)in[filt_pos + 1]);
            ++filt_pos;
        }
    }
    return 0;
}

int hannah_resample(const int16_t *in,  int in_samples,  int src_rate,
                          int16_t *out, int out_samples, int dst_rate) {
    /* When downsampling, low-pass the source below the destination Nyquist
     * first; otherwise frequencies above dst_rate/2 alias into the output.
     * The filter runs streaming over the monotonically increasing source
     * index, keeping only its last two outputs, so no scratch buffer is
     * allocated — important on the ESP32 target. Each call starts the
     * filter's delay line at zero; hannah_resample_ctx() is the stateful
     * counterpart for chunked streaming. */
    hannah_biquad_t bq;
    if (dst_rate < src_rate) {
        biquad_lowpass_init(&bq, 0.45 * dst_rate, src_rate);
    }
    return hannah_resample_core(in, in_samples, src_rate, out, out_samples, dst_rate, &bq);
}

void hannah_resample_ctx_init(hannah_resample_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

int hannah_resample_ctx(hannah_resample_ctx_t *ctx,
                         const int16_t *in,  int in_samples,  int src_rate,
                         int16_t *out, int out_samples, int dst_rate) {
    if (!ctx) {
        return -1;
    }
    int filtering = (dst_rate < src_rate);

    /* (Re)compute the LPF coefficients and reset its delay line on the
     * first call, or whenever src_rate/dst_rate change under us — a rate
     * change starts a new stream as far as the filter is concerned, so it
     * gets its own settling transient rather than reusing stale state. */
    if (filtering && (!ctx->filtering || ctx->src_rate != src_rate || ctx->dst_rate != dst_rate)) {
        hannah_biquad_t fresh;
        biquad_lowpass_init(&fresh, 0.45 * dst_rate, src_rate);
        ctx->b0 = fresh.b0; ctx->b1 = fresh.b1; ctx->b2 = fresh.b2;
        ctx->a1 = fresh.a1; ctx->a2 = fresh.a2;
        ctx->z1 = fresh.z1; ctx->z2 = fresh.z2;
        ctx->src_rate = src_rate;
        ctx->dst_rate = dst_rate;
    }
    ctx->filtering = filtering;

    hannah_biquad_t bq = { ctx->b0, ctx->b1, ctx->b2, ctx->a1, ctx->a2, ctx->z1, ctx->z2 };
    int rc = hannah_resample_core(in, in_samples, src_rate, out, out_samples, dst_rate, &bq);
    if (rc == 0 && filtering) {
        /* Carry the delay line forward for the next chunk. */
        ctx->z1 = bq.z1;
        ctx->z2 = bq.z2;
    }
    return rc;
}

int hannah_vad(const int16_t *pcm, int samples, int window_size, float threshold) {
    if (!pcm || samples == 0 || window_size <= 0) {
        return 0;
    }
    for (int i = 0; i < samples; i += window_size) {
        int n = (i + window_size <= samples) ? window_size : (samples - i);
        if (window_active(pcm + i, n, threshold)) {
            return 1;
        }
    }
    return 0;
}

/* ── Streaming VAD ───────────────────────────────────────────────────────── */

void hannah_vad_stream_init(hannah_vad_state_t *state,
                            int   onset_windows,
                            int   offset_windows,
                            float threshold) {
    memset(state, 0, sizeof(*state));
    state->threshold       = threshold;
    state->onset_windows   = onset_windows;
    state->offset_windows  = offset_windows;
}

int hannah_vad_feed(hannah_vad_state_t *state,
                    const int16_t *buf, int chunk_size) {
    if (!state || !buf || chunk_size <= 0) {
        return HANNAH_VAD_SILENCE;
    }

    int active = window_active(buf, chunk_size, state->threshold);

    if (!state->speaking) {
        /* ── Waiting for speech to start ──────────────────────────────────
         * Count consecutive active chunks.  Reset if a silent chunk arrives
         * so a single loud noise doesn't accumulate toward onset. */
        if (active) {
            state->onset_count++;
            if (state->onset_count >= state->onset_windows) {
                /* Enough consecutive active windows — speech confirmed. */
                state->speaking      = 1;
                state->onset_count   = 0;
                state->offset_count  = 0;
                return HANNAH_VAD_ONSET;
            }
        } else {
            state->onset_count = 0;  /* reset on silence */
        }
        return HANNAH_VAD_SILENCE;

    } else {
        /* ── Speech is active — waiting for it to end ─────────────────────
         * Count consecutive silent chunks.  Reset if speech resumes so
         * short within-utterance pauses don't end the recording. */
        if (!active) {
            state->offset_count++;
            if (state->offset_count >= state->offset_windows) {
                /* Enough consecutive silent windows — utterance over. */
                state->speaking      = 0;
                state->offset_count  = 0;
                state->onset_count   = 0;
                return HANNAH_VAD_OFFSET;
            }
        } else {
            state->offset_count = 0;  /* reset on active window */
        }
        return HANNAH_VAD_SPEECH;
    }
}
