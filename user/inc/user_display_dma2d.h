/*
 * DMA2D 渲染加速封装
 *   - 硬件填充 (fill)
 *   - RGB565 --> YCbCr 4:2:0 的 planar 块状输出 (DMA2D 不支持, 仍用 CPU)
 *   - 矩形内存搬运 (copy)
 *
 * Copyright (c) 2025
 */
#ifndef __USER_DISPLAY_DMA2D_H__
#define __USER_DISPLAY_DMA2D_H__

#include <stdint.h>
#include "user_display_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 DMA2D 外设
 */
int udisp_dma2d_init(void);

/**
 * @brief 反初始化
 */
int udisp_dma2d_deinit(void);

/**
 * @brief 用指定 RGB565 颜色填充整个帧缓冲
 *        硬件加速, 比 CPU 填充快 ~5~10 倍
 * @return UDISP_OK / 负错误码
 */
int udisp_dma2d_clear(udisp_fb_t *fb, uint16_t color_rgb565);

/**
 * @brief 用指定颜色填充指定矩形
 * @param fb     帧缓冲
 * @param x,y    左上角 (像素)
 * @param w,h    宽高 (像素)
 * @param color  RGB565
 */
int udisp_dma2d_fill_rect(udisp_fb_t *fb,
                          int x, int y, int w, int h,
                          uint16_t color_rgb565);

/**
 * @brief 标志 DMA2D 模块是否已就绪 (用于 fallback 到 CPU 实现)
 */
int udisp_dma2d_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISPLAY_DMA2D_H__ */
