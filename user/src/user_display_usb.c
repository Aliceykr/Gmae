/*
 * USB HS Bulk 传输封装 - STUB (Phase 3 将实现)
 *
 * Copyright (c) 2025
 */
#include "user_display_usb.h"
#include "user_display.h"

#define DBG_TAG "udisp.usb"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

int udisp_usb_init(void)
{
    LOG_D("usb bulk: stub, will be implemented in Phase 3");
    return UDISP_ERR_NOT_INIT;
}

int udisp_usb_send_frame(const udisp_frame_hdr_t *hdr,
                         const uint8_t *payload)
{
    (void)hdr; (void)payload;
    return UDISP_ERR_NOT_INIT;
}
