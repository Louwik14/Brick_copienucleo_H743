#include "ch.h"
#include "hal.h"

#include "drivers.h"
#include "drivers/audio/drv_audio.h"

#include <string.h>

#define AUDIO_BEEP_FREQUENCY_HZ 1000U
#define AUDIO_BEEP_ON_MS        200U
#define AUDIO_BEEP_OFF_MS       200U
#define AUDIO_BEEP_AMPLITUDE    ((int32_t)(8388607L * 3L / 5L))

void drv_audio_process_block(const int32_t               *adc_in,
                             const spilink_audio_block_t spi_in,
                             int32_t                     *dac_out,
                             spilink_audio_block_t        spi_out,
                             size_t                       frames) {
    (void)adc_in;
    (void)spi_in;

    static uint32_t beep_sample = 0U;
    static uint32_t tone_phase = 0U;
    const uint32_t samples_per_cycle = AUDIO_SAMPLE_RATE_HZ / AUDIO_BEEP_FREQUENCY_HZ;
    const uint32_t samples_per_on = (AUDIO_SAMPLE_RATE_HZ * AUDIO_BEEP_ON_MS) / 1000U;
    const uint32_t samples_per_off = (AUDIO_SAMPLE_RATE_HZ * AUDIO_BEEP_OFF_MS) / 1000U;
    const uint32_t samples_per_period = samples_per_on + samples_per_off;

    for (size_t n = 0; n < frames; ++n) {
        const bool beep_on = (beep_sample % samples_per_period) < samples_per_on;
        int32_t sample_i = 0;

        if (beep_on) {
            sample_i = (tone_phase < (samples_per_cycle / 2U)) ? AUDIO_BEEP_AMPLITUDE
                                                              : -AUDIO_BEEP_AMPLITUDE;
            tone_phase++;
            if (tone_phase >= samples_per_cycle) {
                tone_phase = 0U;
            }
        } else {
            tone_phase = 0U;
        }

        for (size_t ch = 0; ch < AUDIO_NUM_OUTPUT_CHANNELS; ++ch) {
            dac_out[ch] = sample_i;
        }

        dac_out += AUDIO_NUM_OUTPUT_CHANNELS;
        beep_sample++;
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
