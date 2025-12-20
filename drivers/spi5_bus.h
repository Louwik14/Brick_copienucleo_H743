#ifndef SPI5_BUS_H
#define SPI5_BUS_H

#include "ch.h"

/* Mutex partagé pour le bus SPI5 (OLED + 74HC165). */
extern mutex_t spi5_mutex;

/* Initialisation idempotente du mutex SPI5. */
void spi5_bus_init(void);

#endif /* SPI5_BUS_H */
