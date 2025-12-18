#include "cache_utils.h"

#include "core_cm7.h"

#include <stdint.h>

#define CACHE_LINE_SIZE 32U

bool cache_utils_dcache_enabled(void) {
    return (SCB->CCR & SCB_CCR_DC_Msk) != 0U;
}

void cache_utils_clean_dcache(const void *addr, size_t length) {
    if (!cache_utils_dcache_enabled() || length == 0U || addr == NULL) {
        return;
    }

    /* Ne tente un clean que sur une zone RAM accessible par DMA. */
    const uintptr_t ptr = (uintptr_t)addr;
    if (ptr < 0x20000000U || ptr >= 0x40000000U) {
        return;
    }

    uintptr_t start = ((uintptr_t)addr) & ~(CACHE_LINE_SIZE - 1U);
    uintptr_t end   = ((uintptr_t)addr + length + CACHE_LINE_SIZE - 1U) & ~(CACHE_LINE_SIZE - 1U);

    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}
