/*
 * 硬件 JPEG 编码器封装 - STUB (Phase 2 将实现)
 *
 * Copyright (c) 2025
 */
#include "user_display_jpeg.h"
#include "user_display.h"

#define DBG_TAG "udisp.jpeg"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

int udisp_jpeg_init(void)
{
    LOG_D("jpeg encoder: stub, will be implemented in Phase 2");
    return UDISP_ERR_NOT_INIT;
}

int udisp_jpeg_encode(const udisp_fb_t *fb,
                      uint8_t *out_buf, uint32_t out_cap,
                      uint32_t *out_len)
{
    (void)fb; (void)out_buf; (void)out_cap;
    if (out_len) *out_len = 0;
    return UDISP_ERR_NOT_INIT;
}
