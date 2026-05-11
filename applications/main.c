/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020-09-02     RT-Thread    first version
 * 2025-01-01     Kiro         integrate USB Display Bridge (Phase 1)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "drv_common.h"
#include "user_display.h"

#define LED_PIN GET_PIN(O, 5)

int main(void)
{
    rt_uint32_t count = 1;

    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);

    /* 启动 USB 显示桥. 通过编译宏分段启用子系统, 排查 WHD 冲突:
     *   (无)                      只启用 FB (Phase 1 同款, 已验证)
     *   UDISP_ENABLE_DMA2D        加上 DMA2D
     *   UDISP_ENABLE_JPEG         加上 JPEG
     */
    if (udisp_init() != 0)
    {
        rt_kprintf("[main] udisp_init failed, continue without display bridge\n");
    }

    while(count++)
    {
        rt_thread_mdelay(500);
        rt_pin_write(LED_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED_PIN, PIN_LOW);
    }
    return RT_EOK;
}

#include "stm32h7rsxx.h"
static int vtor_config(void)
{
    /* Vector Table Relocation in Internal XSPI2_BASE */
    SCB->VTOR = XSPI2_BASE;
    return 0;
}
INIT_BOARD_EXPORT(vtor_config);

