/* Copyright 2025 muzkr https://github.com/muzkr
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */

#include "driver/vcp.h"
#include "usb_config.h"
#include "py32f071_ll_bus.h"
#include "py32f071_ll_gpio.h"
#include "driver/system.h"


uint8_t VCP_RxBuf[VCP_RX_BUF_SIZE];
volatile uint32_t VCP_RxBufPointer = 0;

void VCP_Init()
{
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA); // PA12:11

    /* Leave the bus before enumerating (F4HWN 9485a68). The bootloader can hand
     * over with its own USB device still attached: a host that sees no
     * disconnect keeps it and never enumerates this firmware's, so the PC
     * cannot reach the radio until it is switched off and on. Reset the USB
     * peripheral and hold D+ low 50 ms: the host sees the radio unplugged,
     * then plugged again. Boot only: a running session is never dropped. */
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_USBD);
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_12);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_12, LL_GPIO_MODE_OUTPUT);
    SYSTEM_DelayMs(50);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_12, LL_GPIO_MODE_ANALOG);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_USBD);

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USBD);

    cdc_acm_rx_buf_t rx_buf = {
        .buf = VCP_RxBuf,
        .size = sizeof(VCP_RxBuf),
        .write_pointer = &VCP_RxBufPointer,
    };
    cdc_acm_init(rx_buf);

    NVIC_SetPriority(USBD_IRQn, 3);
    NVIC_EnableIRQ(USBD_IRQn);
}
