#ifndef SPI5_BUS_H
#define SPI5_BUS_H

#include "ch.h"
#include "hal.h"

/* Initialisation idempotente du bus SPI5 partagé. */
void spi5_bus_init(void);

/* Acquisition sérialisée du bus SPI5 avec configuration dédiée. */
void spi5_bus_acquire(const SPIConfig *cfg);

/* Libération du bus SPI5. */
void spi5_bus_release(void);

#endif /* SPI5_BUS_H */
