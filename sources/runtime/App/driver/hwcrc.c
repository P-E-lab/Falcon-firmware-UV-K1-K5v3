#include "py32f0xx.h"
#include "py32f071_ll_bus.h"
#include "driver/hwcrc.h"

void HWCRC_Begin(void)
{
    if (!LL_AHB1_GRP1_IsEnabledClock(LL_AHB1_GRP1_PERIPH_CRC))
        LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_CRC);
    CRC->CR = CRC_CR_RESET;
}

void HWCRC_Feed(const void *data, uint32_t bytes)
{
    const uint32_t *word = data;
    for (uint32_t n = bytes / 4u; n; n--)
        CRC->DR = *word++;
}

uint32_t HWCRC_Result(void)
{
    return CRC->DR;
}

uint32_t HWCRC_Compute(const void *data, uint32_t bytes)
{
    HWCRC_Begin();
    HWCRC_Feed(data, bytes);
    return HWCRC_Result();
}
