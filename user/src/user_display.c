/*
 * USB 显示桥主控
 *
 * Copyright (c) 2025
 */
#include "user_display.h"
#include "user_display_fb.h"
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

    LOG_I("Initializing USB Display Bridge (Phase 1: FB only)");

    rc = udisp_fb_init();
    if (rc != UDISP_OK)
    {
        LOG_E("fb_init failed: %d", rc);
        return rc;
    }

    /* Phase 2 / Phase 3 的 stub, 返回 NOT_INIT 不算错 */
    (void)udisp_jpeg_init();
    (void)udisp_usb_init();

    s_ready = RT_TRUE;
    LOG_I("init OK (Phase 1)");
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
