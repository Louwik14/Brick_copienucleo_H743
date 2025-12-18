#include "ch.h"
#include "hal.h"

#include "drivers.h"
#include "drv_display.h"

int main(void) {
    halInit();
    chSysInit();

    drivers_init_all();

    drv_display_clear();
    drv_display_draw_text(0, 0, "OLED TEST");
    drv_display_draw_text(0, 16, "SPI5");
    drv_display_draw_text(0, 32, "CHIBIOS");
    drv_display_update();

    while (true) {
        chThdSleepMilliseconds(1000);
    }
}
