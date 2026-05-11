/*
 * 测试图案生成器
 *
 * 注意: 帧缓冲在 PSRAM 里, 逐像素 CPU 写入会比较慢
 *       800*480 一帧约 380K 像素, 纯 CPU 大约 30~80ms 量级
 *       后续可以考虑用 DMA2D 加速
 *
 * Copyright (c) 2025
 */
#include "user_test_pattern.h"
#include <rtthread.h>
#include <string.h>

#define DBG_TAG "udisp.pat"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ----------------------------- 内部绘制工具 ----------------------------- */

static inline void put_pixel(udisp_fb_t *fb, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= (int)fb->width || y >= (int)fb->height) return;
    fb->pixels[y * fb->width + x] = c;
}

static void fill_rect(udisp_fb_t *fb, int x0, int y0, int w, int h, uint16_t c)
{
    int x1 = x0 + w, y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)fb->width)  x1 = fb->width;
    if (y1 > (int)fb->height) y1 = fb->height;

    for (int y = y0; y < y1; y++)
    {
        uint16_t *row = fb->pixels + y * fb->width + x0;
        for (int x = x0; x < x1; x++)
        {
            *row++ = c;
        }
    }
}

/* ----------------------------- 各种图案 ----------------------------- */

static void pat_solid(udisp_fb_t *fb, uint32_t frame_idx)
{
    /* 每 30 帧循环一种颜色 */
    static const uint16_t palette[] = {
        0xF800, /* red */
        0x07E0, /* green */
        0x001F, /* blue */
        0xFFE0, /* yellow */
        0x07FF, /* cyan */
        0xF81F, /* magenta */
        0xFFFF, /* white */
        0x0000, /* black */
    };
    uint16_t c = palette[(frame_idx / 30) % (sizeof(palette) / sizeof(palette[0]))];
    udisp_fb_clear(fb, c);
}

static void pat_bars(udisp_fb_t *fb, uint32_t frame_idx)
{
    /* 8 条竖彩条 (类似电视测试图) */
    (void)frame_idx;
    static const uint16_t bars[] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
        0xF81F, 0xF800, 0x001F, 0x0000
    };
    int bar_w = fb->width / 8;
    for (int i = 0; i < 8; i++)
    {
        int x0 = i * bar_w;
        int w  = (i == 7) ? (fb->width - x0) : bar_w;
        fill_rect(fb, x0, 0, w, fb->height, bars[i]);
    }
}

static void pat_checker(udisp_fb_t *fb, uint32_t frame_idx)
{
    (void)frame_idx;
    const int cell = 32;
    for (int y = 0; y < (int)fb->height; y++)
    {
        uint16_t *row = fb->pixels + y * fb->width;
        for (int x = 0; x < (int)fb->width; x++)
        {
            row[x] = ((x / cell) ^ (y / cell)) & 1 ? 0xFFFF : 0x0000;
        }
    }
}

static void pat_gradient(udisp_fb_t *fb, uint32_t frame_idx)
{
    /* 水平方向红色渐变, 垂直方向蓝色渐变 */
    (void)frame_idx;
    for (int y = 0; y < (int)fb->height; y++)
    {
        uint8_t b = (uint8_t)((y * 255) / fb->height);
        uint16_t *row = fb->pixels + y * fb->width;
        for (int x = 0; x < (int)fb->width; x++)
        {
            uint8_t r = (uint8_t)((x * 255) / fb->width);
            row[x] = udisp_rgb565(r, 0, b);
        }
    }
}

static void pat_cross(udisp_fb_t *fb, uint32_t frame_idx)
{
    (void)frame_idx;
    udisp_fb_clear(fb, 0x0000);

    /* 边框 (白色) */
    fill_rect(fb, 0, 0, fb->width, 2, 0xFFFF);
    fill_rect(fb, 0, fb->height - 2, fb->width, 2, 0xFFFF);
    fill_rect(fb, 0, 0, 2, fb->height, 0xFFFF);
    fill_rect(fb, fb->width - 2, 0, 2, fb->height, 0xFFFF);

    /* 十字线 (绿色) */
    fill_rect(fb, fb->width / 2 - 1, 0, 2, fb->height, 0x07E0);
    fill_rect(fb, 0, fb->height / 2 - 1, fb->width, 2, 0x07E0);

    /* 中心标志 (红色圆点近似, 用矩形代替) */
    fill_rect(fb, fb->width / 2 - 8, fb->height / 2 - 8, 16, 16, 0xF800);

    /* 角上色块 (便于定位) */
    fill_rect(fb, 4, 4, 30, 30, 0xF800);              /* 左上 红 */
    fill_rect(fb, fb->width - 34, 4, 30, 30, 0x07E0); /* 右上 绿 */
    fill_rect(fb, 4, fb->height - 34, 30, 30, 0x001F); /* 左下 蓝 */
    fill_rect(fb, fb->width - 34, fb->height - 34, 30, 30, 0xFFE0); /* 右下 黄 */
}

static void pat_anim_bar(udisp_fb_t *fb, uint32_t frame_idx)
{
    /* 黑色背景 + 一根白色竖条来回扫 */
    udisp_fb_clear(fb, 0x0000);

    const int bar_w = 20;
    int travel = fb->width - bar_w;
    int phase  = frame_idx % (travel * 2);
    int x      = (phase < travel) ? phase : (travel * 2 - phase);

    fill_rect(fb, x, 0, bar_w, fb->height, 0xFFFF);

    /* 顺便画一个帧序号的 "进度条" 在底部 */
    int progress = (frame_idx % 300) * fb->width / 300;
    fill_rect(fb, 0, fb->height - 8, progress, 8, 0x07E0);
}

/* ----------------------------- 入口 ----------------------------- */

int udisp_draw_test_pattern(udisp_fb_t *fb, udisp_pattern_t pattern, uint32_t frame_idx)
{
    if (fb == RT_NULL || fb->pixels == RT_NULL) return UDISP_ERR_INVAL;

    switch (pattern)
    {
        case UDISP_PATTERN_SOLID:    pat_solid(fb, frame_idx);    break;
        case UDISP_PATTERN_BARS:     pat_bars(fb, frame_idx);     break;
        case UDISP_PATTERN_CHECKER:  pat_checker(fb, frame_idx);  break;
        case UDISP_PATTERN_GRADIENT: pat_gradient(fb, frame_idx); break;
        case UDISP_PATTERN_CROSS:    pat_cross(fb, frame_idx);    break;
        case UDISP_PATTERN_ANIM_BAR: pat_anim_bar(fb, frame_idx); break;
        default:
            LOG_W("unknown pattern %d", pattern);
            return UDISP_ERR_INVAL;
    }
    return UDISP_OK;
}
