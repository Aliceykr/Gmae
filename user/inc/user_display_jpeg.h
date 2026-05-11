/*
 * 硬件 JPEG 编码器封装 (Phase 2, 当前为 stub)
 *
 * Copyright (c) 2025
 */
#ifndef __USER_DISPLAY_JPEG_H__
#define __USER_DISPLAY_JPEG_H__

#include <stdint.h>
#include "user_display_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化硬件 JPEG 编码器
 * @note  Phase 2 实现, 当前返回 -UDISP_ERR_NOT_INIT
 */
int udisp_jpeg_init(void);

/**
 * @brief 编码一帧
 * @param fb        源帧缓冲 (RGB565)
 * @param out_buf   输出缓冲
 * @param out_cap   输出缓冲容量
 * @param out_len   实际编码后的长度 (字节)
 * @return UDISP_OK / 负错误码
 */
int udisp_jpeg_encode(const udisp_fb_t *fb,
                      uint8_t *out_buf, uint32_t out_cap,
                      uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISPLAY_JPEG_H__ */
