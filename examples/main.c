#include "libhannah_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#define SAMPLING_RATE 44100
#define DURATION_MS 100
#define SIGNAL_FREQ 440.0
#define AMPLITUDE 20000

#ifndef M_PI
#   define M_PI 3.1415926535897932384626433832
#endif

void generate_sine_wave(int16_t *buffer, int num_samples) {
    for (int n = 0; n < num_samples; n++) {
        // Berechnung des Sinus-Werts
        double val = AMPLITUDE * sin(2.0 * M_PI * SIGNAL_FREQ * n / SAMPLING_RATE);
        buffer[n] = (int16_t)val;
    }
}

int main() {
    int in_samples = (SAMPLING_RATE * DURATION_MS) / 1000;
    int16_t input_pcm[in_samples];
    
    generate_sine_wave(input_pcm, in_samples);

    int dst_rate = 16000;
    int out_samples = (int)((double)in_samples * dst_rate / SAMPLING_RATE + 0.5);
    int16_t output_pcm[out_samples];

    if (hannah_resample(input_pcm, in_samples, SAMPLING_RATE, output_pcm, out_samples, dst_rate) == 0) {
        printf("Resampled: ");
        for (int i = 0; i < out_samples; ++i) {
            printf("%d ", output_pcm[i]);
        }
        printf("\n");

        /* 512 samples @ 16kHz = 32ms per analysis window */
        hannah_vad(output_pcm, out_samples, 512, 0.7f) ? printf("Voice detected\n") : printf("No voice detected\n");
    } else {
        printf("Resampling failed\n");
    }

    return 0;
}