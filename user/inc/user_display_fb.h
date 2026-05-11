/*
 * 帧缓冲管理 (双缓冲, 位于 PSRAM)
 *
 * Copyright (c) 2025
 */
#ifndef __USER_DISPLAY_FB_H__
#define __USER_DISPLAY_FB_H__

#include <rtthread.h>
#include <stdint.h>
#include "user_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- 内存布局 ----------------------------- */
/* PSRAM 起始 0x90000000, 总共 32MB, 这里为显示子系统预留前 ~2.25MB */

#define UDISP_PSRAM_BASE        (0x90000000UL)

#define UDISP_FB0_ADDR          (UDISP_PSRAM_BASE + 0x00000000UL)   /* 0x90000000 */
#define UDISP_FB1_ADDR          (UDISP_PSRAM_BASE + 0x00100000UL)   /* 0x90100000, +1MB */
#define UDISP_JPEG_OUT_ADDR     (UDISP_PSRAM_BASE + 0x00200000UL)   /* 0x90200000, +2MB */
#define UDISP_JPEG_OUT_SIZE     (256 * 1024)                        /* 256 KB */

/* ----------------------------- 帧缓冲结构 ----------------------------- */

typedef struct udisp_fb
{
    uint16_t *pixels;       /* 指向像素首地址 (RGB565) */
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;       /* 行跨距, 单位字节 */
    uint32_t  size;         /* 总字节数 */
    uint8_t   index;        /* 缓冲索引 0/1 */
} udisp_fb_t;

/* ----------------------------- API ----------------------------- */

/**
 * @brief 初始化帧缓冲子系统, 验证 PSRAM 可访问性
 */
int udisp_fb_init(void);

/**
 * @brief 获取当前的后台缓冲 (供渲染代码写入)
 */
udisp_fb_t *udisp_fb_get_back(void);

/**
 * @brief 获取当前的前台缓冲 (供 JPEG 编码器读取)
 */
udisp_fb_t *udisp_fb_get_front(void);

/**
 * @brief 交换前后缓冲
 *        调用前: 后台缓冲已完成绘制
 *        调用后: 后台缓冲变为可写, 前台缓冲变为可读
 *        会自动 clean D-Cache (确保 DMA/外设能看到最新数据)
 */
void udisp_fb_swap(void);

/**
 * @brief 清空指定缓冲为某个颜色 (RGB565)
 */
void udisp_fb_clear(udisp_fb_t *fb, uint16_t color);

/**
 * @brief 对帧缓冲对应地址做 D-Cache clean, 确保 DMA 看到 CPU 的写入
 */
void udisp_fb_cache_clean(udisp_fb_t *fb);

/* ----------------------------- RGB565 工具 ----------------------------- */

static inline uint16_t udisp_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISPLAY_FB_H__ */
