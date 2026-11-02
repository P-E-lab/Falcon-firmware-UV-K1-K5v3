#pragma once
#include <stdbool.h>
#include "py32f071_ll_spi.h"
/* Independent of SysTick/interrupts, including during DMA recovery. */
static inline bool SPI_WaitStatus(SPI_TypeDef *spi, uint32_t mask, uint32_t value)
{
    uint32_t remaining = 20000u;
    while ((spi->SR & mask) != value)
        if (!--remaining)
            return false;
    return true;
}
