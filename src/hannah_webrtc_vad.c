#include "libhannah_audio.h"
#include "fvad.h"
#include <string.h>

int hannah_webrtc_vad_init(hannah_webrtc_vad_state_t *state,
                            int aggressiveness, int sample_rate,
                            int onset_windows, int offset_windows) {
    if (!state) {
        return -1;
    }
    memset(state, 0, sizeof(*state));

    Fvad *inst = fvad_new();
    if (!inst) {
        return -1;
    }
    if (fvad_set_mode(inst, aggressiveness) != 0 ||
        fvad_set_sample_rate(inst, sample_rate) != 0) {
        fvad_free(inst);
        return -1;
    }

    state->vad_inst       = inst;
    state->sample_rate    = sample_rate;
    state->onset_windows  = onset_windows;
    state->offset_windows = offset_windows;
    return 0;
}

int hannah_webrtc_vad_feed(hannah_webrtc_vad_state_t *state,
                            const int16_t *buf, int chunk_size) {
    if (!state || !state->vad_inst || !buf || chunk_size <= 0) {
        return -1;
    }

    int result = fvad_process((Fvad *)state->vad_inst, buf, (size_t)chunk_size);
    if (result < 0) {
        return -1;  /* chunk_size isn't a valid 10/20/30ms frame */
    }
    int active = result;

    if (!state->speaking) {
        /* Waiting for speech to start; reset on silence so a single active
         * frame doesn't accumulate toward onset (mirrors hannah_vad_feed). */
        if (active) {
            state->onset_count++;
            if (state->onset_count >= state->onset_windows) {
                state->speaking      = 1;
                state->onset_count   = 0;
                state->offset_count  = 0;
                return HANNAH_VAD_ONSET;
            }
        } else {
            state->onset_count = 0;
        }
        return HANNAH_VAD_SILENCE;

    } else {
        /* Speech active; reset on activity so short within-utterance pauses
         * don't end it. */
        if (!active) {
            state->offset_count++;
            if (state->offset_count >= state->offset_windows) {
                state->speaking      = 0;
                state->offset_count  = 0;
                state->onset_count   = 0;
                return HANNAH_VAD_OFFSET;
            }
        } else {
            state->offset_count = 0;
        }
        return HANNAH_VAD_SPEECH;
    }
}

void hannah_webrtc_vad_free(hannah_webrtc_vad_state_t *state) {
    if (state && state->vad_inst) {
        fvad_free((Fvad *)state->vad_inst);
        state->vad_inst = NULL;
    }
}
