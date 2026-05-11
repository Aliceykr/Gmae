/*
 * 硬件 JPEG 编码器封装
 *
 * Phase 2.5 流水线:
 *   FB (PSRAM, RGB565)
 *     ↓ (CPU, 每次处理 1 行 MCU = 16 像素高)
 *   MCU 条带 (SRAM, 2 × 24KB 交替)
 *     ↓ (JPEG 外设通过 GetData 回调逐条带消费)
 *   JPEG 输出 (PSRAM, 256KB)
 *
 * 关键点:
 *   - MCU 条带驻留 SRAM, 避免 CPU 小粒度写 PSRAM 的带宽瓶颈
 *   - convert 和 encode 通过 HAL_JPEG 的回调天然流水线
 *   - JPEG 用 IT (中断) 模式, 避免 DMA 对 MCU 条带 cache invalidate 复杂度
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

/* 一行 MCU 条带的大小 (1 行 = 50 个 MCU 的字节数) */
#define UDISP_JPEG_STRIPE_MCUS      UDISP_JPEG_MCU_COUNT_X                    /* 50 */
#define UDISP_JPEG_STRIPE_BYTES     (UDISP_JPEG_STRIPE_MCUS * UDISP_JPEG_MCU_BYTES) /* 19200 */

/* 双条带缓冲总大小 */
#define UDISP_JPEG_STRIPE_COUNT     2
#define UDISP_JPEG_STRIPE_POOL_SIZE (UDISP_JPEG_STRIPE_BYTES * UDISP_JPEG_STRIPE_COUNT) /* 38.4 KB */

/* ----------------------------- API ----------------------------- */

int udisp_jpeg_init(void);
int udisp_jpeg_deinit(void);

/**
 * @brief 编码一帧 JPEG
 *        内部自动做 convert→encode 条带流水线
 * @return UDISP_OK / 负错误码
 */
int udisp_jpeg_encode(const udisp_fb_t *fb,
                      uint8_t *out_buf, uint32_t out_cap,
                      uint32_t *out_len);

uint8_t *udisp_jpeg_get_output_buffer(void);
uint32_t udisp_jpeg_get_output_capacity(void);

typedef struct
{
    uint32_t last_convert_us;       /* 累计 convert 耗时 (us) */
    uint32_t last_encode_us;        /* 总 pipeline 耗时 (us) */
    uint32_t last_tick_ms;          /* 基于 tick 的交叉验证 (ms) */
    uint32_t last_stripe_cnt;       /* 实际喂给 JPEG 的条带数 (应为 30) */
    uint32_t last_jpeg_bytes;
    uint32_t total_frames;
} udisp_jpeg_stats_t;

void udisp_jpeg_get_stats(udisp_jpeg_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISPLAY_JPEG_H__ */
