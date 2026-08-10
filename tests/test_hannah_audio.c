#include "libhannah_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int float_eq(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

static void test_stereo_to_mono(void) {
    int16_t in[]  = {2, 4, 6, 8, 1, 2};
    int16_t out[3];

    assert(hannah_stereo_to_mono(in, out, 3) == 0);
    assert(out[0] == 3);  /* (2+4)/2 */
    assert(out[1] == 7);  /* (6+8)/2 */
    assert(out[2] == 1);  /* (1+2)/2, truncated toward zero */

    assert(hannah_stereo_to_mono(NULL, out, 3) == -1);
    assert(hannah_stereo_to_mono(in, NULL, 3) == -1);
    assert(hannah_stereo_to_mono(in, out, 0) == -1);

    printf("test_stereo_to_mono OK\n");
}

static void test_rms(void) {
    int16_t silence[4] = {0, 0, 0, 0};
    int16_t full_scale[2] = {32767, -32767};

    assert(float_eq(hannah_rms(silence, 4), 0.0f, 1e-6f));
    assert(float_eq(hannah_rms(full_scale, 2), 1.0f, 1e-4f));

    assert(hannah_rms(NULL, 4) == 0.0f);
    assert(hannah_rms(silence, 0) == 0.0f);

    printf("test_rms OK\n");
}

static void test_resample(void) {
    int16_t in[4] = {100, 200, 300, 400};
    int16_t out_identity[4];

    /* identity: src_rate == dst_rate (no filtering, exact passthrough) */
    assert(hannah_resample(in, 4, 16000, out_identity, 4, 16000) == 0);
    for (int i = 0; i < 4; ++i) {
        assert(out_identity[i] == in[i]);
    }

    /* downsample 16kHz -> 8kHz halves the sample count */
    int16_t out_down[2];
    assert(hannah_resample(in, 4, 16000, out_down, 2, 8000) == 0);

    /* bad arguments */
    assert(hannah_resample(NULL, 4, 16000, out_identity, 4, 16000) == -1);
    assert(hannah_resample(in, 4, 16000, out_identity, 3, 8000) == -1);
    assert(hannah_resample(in, 4, 0, out_identity, 4, 16000) == -1);

    printf("test_resample OK\n");
}

static void test_resample_antialiasing(void) {
    /* An alternating +/- signal at 16kHz is the 8kHz source-Nyquist tone.
     * Naive 2:1 decimation would pick every other sample and alias it down to
     * a large DC offset; the anti-aliasing low-pass must suppress it instead.
     * (Skip the filter's start-up transient before measuring.) */
    int16_t alias_in[256];
    for (int i = 0; i < 256; ++i) {
        alias_in[i] = (i % 2 == 0) ? 10000 : -10000;
    }
    int16_t alias_out[128];
    assert(hannah_resample(alias_in, 256, 16000, alias_out, 128, 8000) == 0);
    assert(hannah_rms(alias_out + 32, 96) < hannah_rms(alias_in, 256) * 0.2f);

    /* A low-frequency (DC) signal must still pass essentially unchanged. */
    int16_t flat_in[256];
    for (int i = 0; i < 256; ++i) {
        flat_in[i] = 8000;
    }
    int16_t flat_out[128];
    assert(hannah_resample(flat_in, 256, 16000, flat_out, 128, 8000) == 0);
    assert(float_eq(hannah_rms(flat_out + 32, 96), hannah_rms(flat_in, 256), 0.02f));

    printf("test_resample_antialiasing OK\n");
}

static void test_resample_ctx(void) {
    /* A mic task feeding fixed 10ms chunks (480 samples @ 48kHz -> 160 @
     * 16kHz) through the same ctx must get bit-identical output to a single
     * one-shot call on the whole concatenated buffer — i.e. one settling
     * transient at stream start, not one per chunk. */
    enum { CHUNKS = 10, IN_CHUNK = 480, OUT_CHUNK = 160 };
    int16_t full_in[CHUNKS * IN_CHUNK];
    for (int i = 0; i < CHUNKS * IN_CHUNK; ++i) {
        /* Mix of a low tone (survives the LPF) and near-Nyquist content
         * (must be suppressed) so a broken filter state shows up clearly. */
        full_in[i] = (int16_t)(8000.0 * sin(2.0 * M_PI * 300.0 * i / 48000.0))
                   + ((i % 2 == 0) ? 4000 : -4000);
    }

    int16_t oneshot_out[CHUNKS * OUT_CHUNK];
    assert(hannah_resample(full_in, CHUNKS * IN_CHUNK, 48000,
                            oneshot_out, CHUNKS * OUT_CHUNK, 16000) == 0);

    hannah_resample_ctx_t ctx;
    hannah_resample_ctx_init(&ctx);
    int16_t chunked_out[CHUNKS * OUT_CHUNK];
    for (int c = 0; c < CHUNKS; ++c) {
        assert(hannah_resample_ctx(&ctx,
                                    full_in + c * IN_CHUNK, IN_CHUNK, 48000,
                                    chunked_out + c * OUT_CHUNK, OUT_CHUNK, 16000) == 0);
    }

    for (int i = 0; i < CHUNKS * OUT_CHUNK; ++i) {
        assert(chunked_out[i] == oneshot_out[i]);
    }

    /* Passthrough (no downsampling) must still behave like hannah_resample. */
    hannah_resample_ctx_t ctx_up;
    hannah_resample_ctx_init(&ctx_up);
    int16_t out_identity[IN_CHUNK];
    assert(hannah_resample_ctx(&ctx_up, full_in, IN_CHUNK, 16000,
                                out_identity, IN_CHUNK, 16000) == 0);
    for (int i = 0; i < IN_CHUNK; ++i) {
        assert(out_identity[i] == full_in[i]);
    }

    /* bad arguments */
    assert(hannah_resample_ctx(NULL, full_in, IN_CHUNK, 48000,
                                chunked_out, OUT_CHUNK, 16000) == -1);
    assert(hannah_resample_ctx(&ctx, NULL, IN_CHUNK, 48000,
                                chunked_out, OUT_CHUNK, 16000) == -1);

    printf("test_resample_ctx OK\n");
}

static void test_vad(void) {
    int16_t silence[256] = {0};
    int16_t loud[256];
    for (int i = 0; i < 256; ++i) {
        loud[i] = (i % 2 == 0) ? 10000 : -10000;
    }

    assert(hannah_vad(silence, 256, 128, 0.05f) == 0);
    assert(hannah_vad(loud, 256, 128, 0.05f) == 1);

    assert(hannah_vad(NULL, 256, 128, 0.05f) == 0);
    assert(hannah_vad(silence, 256, 0, 0.05f) == 0);

    printf("test_vad OK\n");
}

static void test_vad_stream(void) {
    int16_t silence[64] = {0};
    int16_t loud[64];
    for (int i = 0; i < 64; ++i) {
        loud[i] = (i % 2 == 0) ? 10000 : -10000;
    }

    hannah_vad_state_t vad;
    hannah_vad_stream_init(&vad, 2, 2, 0.05f);

    assert(hannah_vad_feed(&vad, loud, 64) == HANNAH_VAD_SILENCE);
    assert(hannah_vad_feed(&vad, loud, 64) == HANNAH_VAD_ONSET);
    assert(hannah_vad_feed(&vad, loud, 64) == HANNAH_VAD_SPEECH);
    assert(hannah_vad_feed(&vad, silence, 64) == HANNAH_VAD_SPEECH);
    assert(hannah_vad_feed(&vad, silence, 64) == HANNAH_VAD_OFFSET);
    assert(hannah_vad_feed(&vad, silence, 64) == HANNAH_VAD_SILENCE);

    printf("test_vad_stream OK\n");
}

static void test_webrtc_vad(void) {
    hannah_webrtc_vad_state_t vad;

    /* bad init arguments */
    assert(hannah_webrtc_vad_init(NULL, 2, 16000, 2, 2) == -1);
    assert(hannah_webrtc_vad_init(&vad, 4, 16000, 2, 2) == -1);      /* aggressiveness out of range */
    assert(hannah_webrtc_vad_init(&vad, 2, 44100, 2, 2) == -1);      /* unsupported sample rate */

    assert(hannah_webrtc_vad_init(&vad, 2, 16000, 2, 2) == 0);

    /* 20ms @ 16kHz = 320 samples; anything else must be rejected */
    int16_t silence[320] = {0};
    assert(hannah_webrtc_vad_feed(&vad, silence, 100) == -1);
    assert(hannah_webrtc_vad_feed(NULL, silence, 320) == -1);

    /* silence must never trigger onset, no matter how many frames pass */
    for (int i = 0; i < 10; ++i) {
        assert(hannah_webrtc_vad_feed(&vad, silence, 320) == HANNAH_VAD_SILENCE);
    }

    hannah_webrtc_vad_free(&vad);
    hannah_webrtc_vad_free(NULL);  /* must not crash */

    printf("test_webrtc_vad OK\n");
}

int main(void) {
    test_stereo_to_mono();
    test_rms();
    test_resample();
    test_resample_antialiasing();
    test_resample_ctx();
    test_vad();
    test_vad_stream();
    test_webrtc_vad();

    printf("All tests passed.\n");
    return 0;
}
