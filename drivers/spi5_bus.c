#include "spi5_bus.h"

#include <stdbool.h>

/* Mutex partagé pour préserver l'atomicité des transactions SPI5. */
mutex_t spi5_mutex;

static bool spi5_initialized = false;
static const SPIConfig *current_cfg = NULL;

static inline void spi5_require_initialized(void) {
    if (!spi5_initialized) {
        spi5_bus_init();
    }
}

static inline bool spi5_is_locked_by_me(void) {
    return chMtxGetOwner(&spi5_mutex) == chThdGetSelfX();
}

void spi5_bus_init(void) {
    chSysLock();

    if (!spi5_initialized) {
        chMtxObjectInit(&spi5_mutex);
        spi5_initialized = true;
    }

    chSysUnlock();
}

void spi5_bus_lock(void) {
    spi5_require_initialized();
    chMtxLock(&spi5_mutex);
}

void spi5_bus_unlock(void) {
    chMtxUnlock(&spi5_mutex);
}

static bool spi5_driver_ready(void) {
    return (SPID5.state == SPI_READY) || (SPID5.state == SPI_ACTIVE);
}

bool spi5_bus_is_started_with(const SPIConfig *config) {
    spi5_require_initialized();
    return spi5_driver_ready() && (current_cfg == config);
}

void spi5_bus_start(const SPIConfig *config) {
    osalDbgCheck(config != NULL);
    spi5_require_initialized();
    osalDbgAssert(spi5_is_locked_by_me(), "spi5_bus_start sans lock SPI5");

    if (!spi5_driver_ready() || current_cfg != config) {
        spiStop(&SPID5);
        spiStart(&SPID5, config);
        current_cfg = config;
    }
}
