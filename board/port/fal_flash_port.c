/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-01-01     Kiro         FAL flash port for XSPI2 NOR Flash (Winbond 64MB)
 */

#include <rtthread.h>
#include <fal.h>

#ifdef RT_USING_FAL

#define NOR_FLASH_SIZE          (64 * 1024 * 1024)  /* 64 MB */
#define NOR_FLASH_BLOCK_SIZE    (4096)              /* 4 KB erase granularity */

/* XSPI2 NOR Flash is mapped at 0x70000000 for XIP execution */
#define NOR_FLASH_MAP_ADDR      (0x70000000)

static int nor_flash_init(void)
{
    /* XSPI2 NOR Flash is already initialized by bootloader for XIP */
    return 0;
}

static int nor_flash_read(long offset, uint8_t *buf, size_t size)
{
    /* In XIP mode, we can directly read from the memory-mapped address */
    rt_memcpy(buf, (const void *)(NOR_FLASH_MAP_ADDR + offset), size);
    return size;
}

static int nor_flash_write(long offset, const uint8_t *buf, size_t size)
{
    /* Write operation is not supported in XIP mode */
    /* WiFi firmware should be pre-programmed into flash */
    rt_kprintf("Warning: nor_flash0 write not supported in XIP mode\n");
    return -1;
}

static int nor_flash_erase(long offset, size_t size)
{
    /* Erase operation is not supported in XIP mode */
    rt_kprintf("Warning: nor_flash0 erase not supported in XIP mode\n");
    return -1;
}

struct fal_flash_dev nor_flash0 =
{
    .name       = "norflash0",
    .addr       = 0,
    .len        = NOR_FLASH_SIZE,
    .blk_size   = NOR_FLASH_BLOCK_SIZE,
    .ops        = {nor_flash_init, nor_flash_read, nor_flash_write, nor_flash_erase},
    .write_gran = 1
};

#endif /* RT_USING_FAL */
