/*
 * USB 显示桥主控
 *
 * Copyright (c) 2025
 */
#include "user_display.h"
#include "user_display_fb.h"
#include "user_display_dma2d.h"
#include "user_display_jpeg.h"
#include "user_display_usb.h"

#define DBG_TAG "udisp"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static rt_bool_t s_ready = RT_FALSE;

int udisp_init(void)
{
    int rc;

    if (s_ready)
    {
        LOG_W("already initialized");
        return UDISP_OK;
    }

    LOG_I("Initializing USB Display Bridge (Phase 2: FB + DMA2D + JPEG)");

    rc = udisp_fb_init();
    if (rc != UDISP_OK)
    {
        LOG_E("fb_init failed: %d", rc);
        return rc;
    }

#ifdef UDISP_ENABLE_DMA2D
    /* DMA2D 硬件加速, 失败不致命 (会 fallback 到 CPU) */
    rc = udisp_dma2d_init();
    if (rc != UDISP_OK)
    {
        LOG_W("dma2d_init failed: %d (will fallback to CPU fill)", rc);
    }
    LOG_I("DMA2D phase enabled");
#else
    LOG_W("DMA2D init skipped (UDISP_ENABLE_DMA2D not defined)");
#endif

#ifdef UDISP_ENABLE_JPEG
    /* JPEG 编码器, 失败不致命 (只影响 Phase 2/3 的 jpeg 命令) */
    rc = udisp_jpeg_init();
    if (rc != UDISP_OK)
    {
        LOG_W("jpeg_init failed: %d (JPEG encoding disabled)", rc);
    }
#else
    LOG_W("JPEG init skipped (UDISP_ENABLE_JPEG not defined)");
#endif

    /* Phase 3 stub */
    (void)udisp_usb_init();

    s_ready = RT_TRUE;
    LOG_I("init OK");
    return UDISP_OK;
}

int udisp_deinit(void)
{
    /* 预留, Phase 1 暂无需要释放的资源 */
    s_ready = RT_FALSE;
    return UDISP_OK;
}

rt_bool_t udisp_is_ready(void)
{
    return s_ready;
}
