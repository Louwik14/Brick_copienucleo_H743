#ifndef SPI5_BUS_H
#define SPI5_BUS_H

#include "ch.h"
#include "hal.h"

/* Mutex partagé pour le bus SPI5 (OLED + 74HC165). */
extern mutex_t spi5_mutex;

/* Initialisation idempotente du mutex SPI5. */
void spi5_bus_init(void);

/* Verrou explicite du bus SPI5 : transaction atomique garantie tant que verrou tenu. */
void spi5_bus_lock(void);
void spi5_bus_unlock(void);

/* Applique une configuration SPI5 (cfg1/cfg2) une seule fois par transaction. */
void spi5_bus_start(const SPIConfig *config);

/* Vérifie si le driver SPI5 est configuré/actif avec la configuration passée. */
bool spi5_bus_is_started_with(const SPIConfig *config);

#endif /* SPI5_BUS_H */
