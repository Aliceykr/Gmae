/*
 * USB 显示桥 - 主 API
 *
 * 把板子帧缓冲通过 USB HS Bulk + 硬件 JPEG 编码传到 PC 显示
 *
 * Copyright (c) 2025
 */
#ifndef __USER_DISPLAY_H__
#define __USER_DISPLAY_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- 显示参数 ----------------------------- */

#define UDISP_WIDTH             800
#define UDISP_HEIGHT            480
#define UDISP_BPP               2       /* RGB565 = 2 Bytes / pixel */
#define UDISP_FB_SIZE           (UDISP_WIDTH * UDISP_HEIGHT * UDISP_BPP)

/* 色彩格式 - 目前固定 RGB565, 后续可扩展 RGB888 */
#define UDISP_PIXEL_FMT_RGB565  0

/* ----------------------------- 错误码 ----------------------------- */

#define UDISP_OK                0
#define UDISP_ERR              -1
#define UDISP_ERR_NOMEM        -2
#define UDISP_ERR_BUSY         -3
#define UDISP_ERR_NOT_INIT     -4
#define UDISP_ERR_INVAL        -5

/* ----------------------------- 主 API ----------------------------- */

/**
 * @brief 初始化显示桥子系统
 *        分配帧缓冲, 验证 PSRAM 可用性
 * @return UDISP_OK / 负错误码
 */
int udisp_init(void);

/**
 * @brief 反初始化
 */
int udisp_deinit(void);

/**
 * @brief 判断是否已初始化
 */
rt_bool_t udisp_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISPLAY_H__ */
