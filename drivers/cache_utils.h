#ifndef CACHE_UTILS_H
#define CACHE_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Détection DCache et maintenance minimale pour buffers DMA. */

/* Retourne true si le D-Cache du Cortex-M7 est actif. */
bool cache_utils_dcache_enabled(void);

/* Clean d'un buffer (aligné 32 octets) si le D-Cache est actif. */
void cache_utils_clean_dcache(const void *addr, size_t length);

#endif /* CACHE_UTILS_H */
