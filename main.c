#include "ch.h"
#include "hal.h"

#include "drivers.h"
#include "drivers/audio/drv_audio.h"

#include <math.h>
#include <string.h>

#define AUDIO_TEST_FREQUENCY_HZ 440.0f
#define AUDIO_TEST_AMPLITUDE    0.2f

void drv_audio_process_block(const int32_t               *adc_in,
                             const spilink_audio_block_t spi_in,
                             int32_t                     *dac_out,
                             spilink_audio_block_t        spi_out,
                             size_t                       frames) {
    (void)adc_in;
    (void)spi_in;

    static float phase = 0.0f;
    const float phase_inc = 2.0f * (float)M_PI * (AUDIO_TEST_FREQUENCY_HZ / (float)AUDIO_SAMPLE_RATE_HZ);
    const float scale = AUDIO_TEST_AMPLITUDE * 8388607.0f;

    for (size_t n = 0; n < frames; ++n) {
        float sample = sinf(phase) * scale;
        int32_t sample_i = (int32_t)sample;

        dac_out[0] = sample_i;
        dac_out[1] = sample_i;
        dac_out[2] = sample_i;
        dac_out[3] = sample_i;

        dac_out += AUDIO_NUM_OUTPUT_CHANNELS;
        phase += phase_inc;
        if (phase >= (2.0f * (float)M_PI)) {
            phase -= 2.0f * (float)M_PI;
        }
    }

    if (spi_out != NULL) {
        memset(spi_out, 0, sizeof(spilink_audio_block_t));
    }
}

int main(void) {
    halInit();
    chSysInit();

    drv_audio_init();
    drv_audio_start();

    while (true) {
        chThdSleepMilliseconds(1000);
    }
}
