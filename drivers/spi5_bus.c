#include "spi5_bus.h"
#include <stdbool.h>

mutex_t spi5_mutex;

static bool spi5_initialized = false;

void spi5_bus_init(void) {
    chSysLock();

    if (!spi5_initialized) {
        chMtxObjectInit(&spi5_mutex);
        spi5_initialized = true;
    }

    chSysUnlock();
}
