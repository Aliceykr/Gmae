/*
 * 硬件 JPEG 编码器封装
 *
 * 流程:
 *   [FB RGB565] --CPU转换--> [MCU YCbCr 4:2:0] --HW JPEG--> [JPEG输出]
 *
 * 使用 4:2:0 色度子采样:
 *   - 每个 MCU = 16x16 像素 = 4 个 Y 块(8x8) + 1 个 Cb(8x8) + 1 个 Cr(8x8) = 384 字节
 *   - 800x480 @ 4:2:0 刚好整除 -> 50 x 30 = 1500 个 MCU 块
 *   - 整帧 MCU 缓冲 = 1500 * 384 = 576000 字节 (576 KB)
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

/* ----------------------------- 配置 ----------------------------- */

#define UDISP_JPEG_QUALITY          80      /* 1..100, 推荐 70~90 */
#define UDISP_JPEG_MCU_WIDTH        16
#define UDISP_JPEG_MCU_HEIGHT       16
#define UDISP_JPEG_MCU_COUNT_X      (UDISP_WIDTH  / UDISP_JPEG_MCU_WIDTH)     /* 50 */
#define UDISP_JPEG_MCU_COUNT_Y      (UDISP_HEIGHT / UDISP_JPEG_MCU_HEIGHT)    /* 30 */
#define UDISP_JPEG_MCU_TOTAL        (UDISP_JPEG_MCU_COUNT_X * UDISP_JPEG_MCU_COUNT_Y) /* 1500 */
#define UDISP_JPEG_MCU_BYTES        384                                       /* 6*8*8 */
#define UDISP_JPEG_MCU_IN_SIZE      (UDISP_JPEG_MCU_TOTAL * UDISP_JPEG_MCU_BYTES)     /* 576000 */

/* ----------------------------- API ----------------------------- */

/**
 * @brief 初始化硬件 JPEG 编码器 (JPEG 外设时钟、HAL 句柄)
 */
int udisp_jpeg_init(void);

/**
 * @brief 反初始化
 */
int udisp_jpeg_deinit(void);

/**
 * @brief 把 RGB565 帧缓冲转换为 JPEG 所需的 4:2:0 MCU 块排列
 * @param fb      源帧缓冲 (必须 800x480 RGB565)
 * @param mcu_buf 目标 MCU 缓冲 (大小必须 >= UDISP_JPEG_MCU_IN_SIZE)
 * @return UDISP_OK / 负错误码
 */
int udisp_jpeg_fb_to_mcu(const udisp_fb_t *fb, uint8_t *mcu_buf);

/**
 * @brief 编码一帧 JPEG (阻塞式)
 * @param fb        源帧缓冲
 * @param out_buf   输出缓冲
 * @param out_cap   输出缓冲容量
 * @param out_len   实际编码后的长度 (字节)
 * @return UDISP_OK / 负错误码
 *
 * @note 内部会自动调用 fb_to_mcu + HAL_JPEG_Encode
 */
int udisp_jpeg_encode(const udisp_fb_t *fb,
                      uint8_t *out_buf, uint32_t out_cap,
                      uint32_t *out_len);

/**
 * @brief 获取内部 MCU 缓冲地址 (预分配在 PSRAM)
 */
uint8_t *udisp_jpeg_get_mcu_buffer(void);

/**
 * @brief 获取内部 JPEG 输出缓冲地址 / 容量 (预分配在 PSRAM)
 */
uint8_t *udisp_jpeg_get_output_buffer(void);
uint32_t udisp_jpeg_get_output_capacity(void);

/**
 * @brief 统计信息 (上次编码耗时等)
 */
typedef struct
{
    uint32_t last_convert_us;       /* RGB565->YCbCr 耗时 (微秒) */
    uint32_t last_encode_us;        /* HAL_JPEG_Encode 耗时 */
    uint32_t last_jpeg_bytes;       /* 最近一帧 JPEG 字节数 */
    uint32_t total_frames;
} udisp_jpeg_stats_t;

void udisp_jpeg_get_stats(udisp_jpeg_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISPLAY_JPEG_H__ */
