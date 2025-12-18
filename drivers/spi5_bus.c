#include "spi5_bus.h"
#include "hal.h"

#include <stdbool.h>

static mutex_t spi5_mutex;
static bool spi5_initialized = false;
static const SPIConfig *current_cfg = NULL;

void spi5_bus_init(void) {
  chSysLock();

  if (!spi5_initialized) {
    chMtxObjectInit(&spi5_mutex);
    spi5_initialized = true;
  }

  chSysUnlock();
}

void spi5_bus_acquire(const SPIConfig *cfg) {
  osalDbgCheck(cfg != NULL);

  if (!spi5_initialized) {
    spi5_bus_init();
  }

  chMtxLock(&spi5_mutex);

  /* Reconfigure the peripheral only when the requested configuration changes.
     This keeps the time spent under mutex bounded and prevents CPOL/CPHA churn
     when multiple SPI clients share SPI5. */
  if ((SPID5.state != SPI_READY) || (current_cfg != cfg)) {
    spiStop(&SPID5);
    spiStart(&SPID5, cfg);
    current_cfg = cfg;
  }
}

void spi5_bus_release(void) {
  if (!spi5_initialized) {
    return;
  }

  chMtxUnlock(&spi5_mutex);
}
