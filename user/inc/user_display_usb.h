/*
 * USB HS Bulk 传输封装 (Phase 3, 当前为 stub)
 *
 * Copyright (c) 2025
 */
#ifndef __USER_DISPLAY_USB_H__
#define __USER_DISPLAY_USB_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 协议帧头 (16 字节, 固定大小, 便于 PC 端同步) */
#define UDISP_MAGIC             0x55444953   /* 'UDIS' 的小端值 */
#define UDISP_PROTO_VERSION     1

typedef struct __attribute__((packed)) udisp_frame_hdr
{
    uint32_t magic;         /* UDISP_MAGIC */
    uint16_t version;       /* UDISP_PROTO_VERSION */
    uint16_t flags;         /* bit0: 1=JPEG, 0=raw RGB565 */
    uint32_t frame_idx;     /* 帧序号, 单调递增 */
    uint32_t payload_len;   /* JPEG/像素数据长度 */
} udisp_frame_hdr_t;

/**
 * @brief 初始化 USB Bulk 端点
 * @note  Phase 3 实现, 当前返回 -UDISP_ERR_NOT_INIT
 */
int udisp_usb_init(void);

/**
 * @brief 发送一帧
 * @param hdr      帧头 (调用者填写)
 * @param payload  载荷 (已编码的 JPEG 数据)
 * @return UDISP_OK / 负错误码
 */
int udisp_usb_send_frame(const udisp_frame_hdr_t *hdr,
                         const uint8_t *payload);

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISPLAY_USB_H__ */
