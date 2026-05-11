/*
 * 帧缓冲管理实现
 *
 * Copyright (c) 2025
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "user_display_fb.h"
#include "stm32h7rsxx.h"

#define DBG_TAG "udisp.fb"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ----------------------------- 内部状态 ----------------------------- */

static udisp_fb_t s_fb[2];
static uint8_t    s_back_idx = 0;       /* 后台 (CPU 写) */
static uint8_t    s_front_idx = 1;      /* 前台 (外设/DMA 读) */
static rt_bool_t  s_inited = RT_FALSE;
static struct rt_mutex s_swap_mtx;

/* ----------------------------- 工具 ----------------------------- */

/* 测试 PSRAM 某段地址可读写, 用于初始化时快速验证 */
static int psram_quick_test(volatile uint32_t *addr, uint32_t words)
{
    const uint32_t patterns[] = {0xA5A5A5A5, 0x5A5A5A5A, 0xFFFFFFFF, 0x00000000, 0xDEADBEEF};
    uint32_t i, p;

    for (p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++)
    {
        for (i = 0; i < words; i++)
        {
            addr[i] = patterns[p] ^ i;
        }
        /* 写入后 flush 一下 cache, 确保真的写到 PSRAM (不是只在 cache 里) */
        SCB_CleanDCache_by_Addr((uint32_t *)addr, words * 4);

        /* 再从 PSRAM 读回来验证. 需要先 invalidate cache */
        SCB_InvalidateDCache_by_Addr((uint32_t *)addr, words * 4);
        for (i = 0; i < words; i++)
        {
            if (addr[i] != (patterns[p] ^ i))
            {
                LOG_E("PSRAM test fail at 0x%08x: expect 0x%08x got 0x%08x",
                      (uint32_t)&addr[i], (patterns[p] ^ i), addr[i]);
                return -1;
            }
        }
    }
    return 0;
}

/* ----------------------------- API ----------------------------- */

int udisp_fb_init(void)
{
    if (s_inited)
    {
        return UDISP_OK;
    }

    LOG_I("init: fb0=0x%08x fb1=0x%08x size=%u KB each",
          UDISP_FB0_ADDR, UDISP_FB1_ADDR, UDISP_FB_SIZE / 1024);

    /* 简单健康检查: 在两块 FB 的首部各验证 256 字节 */
    if (psram_quick_test((volatile uint32_t *)UDISP_FB0_ADDR, 64) != 0)
    {
        LOG_E("FB0 PSRAM test failed, PSRAM maybe not initialized by bootloader");
        return UDISP_ERR;
    }
    if (psram_quick_test((volatile uint32_t *)UDISP_FB1_ADDR, 64) != 0)
    {
        LOG_E("FB1 PSRAM test failed");
        return UDISP_ERR;
    }

    s_fb[0].pixels = (uint16_t *)UDISP_FB0_ADDR;
    s_fb[0].width  = UDISP_WIDTH;
    s_fb[0].height = UDISP_HEIGHT;
    s_fb[0].stride = UDISP_WIDTH * UDISP_BPP;
    s_fb[0].size   = UDISP_FB_SIZE;
    s_fb[0].index  = 0;

    s_fb[1].pixels = (uint16_t *)UDISP_FB1_ADDR;
    s_fb[1].width  = UDISP_WIDTH;
    s_fb[1].height = UDISP_HEIGHT;
    s_fb[1].stride = UDISP_WIDTH * UDISP_BPP;
    s_fb[1].size   = UDISP_FB_SIZE;
    s_fb[1].index  = 1;

    s_back_idx  = 0;
    s_front_idx = 1;

    /* 清空为黑色 */
    udisp_fb_clear(&s_fb[0], 0x0000);
    udisp_fb_clear(&s_fb[1], 0x0000);

    rt_mutex_init(&s_swap_mtx, "udisp_fb", RT_IPC_FLAG_PRIO);
    s_inited = RT_TRUE;

    LOG_I("init done, double-buffer ready");
    return UDISP_OK;
}

udisp_fb_t *udisp_fb_get_back(void)
{
    if (!s_inited) return RT_NULL;
    return &s_fb[s_back_idx];
}

udisp_fb_t *udisp_fb_get_front(void)
{
    if (!s_inited) return RT_NULL;
    return &s_fb[s_front_idx];
}

void udisp_fb_swap(void)
{
    if (!s_inited) return;

    /* 确保 CPU 的所有写入都 flush 到 PSRAM, DMA/外设能看到最新数据 */
    udisp_fb_cache_clean(&s_fb[s_back_idx]);

    rt_mutex_take(&s_swap_mtx, RT_WAITING_FOREVER);
    uint8_t tmp = s_back_idx;
    s_back_idx  = s_front_idx;
    s_front_idx = tmp;
    rt_mutex_release(&s_swap_mtx);
}

void udisp_fb_clear(udisp_fb_t *fb, uint16_t color)
{
    if (fb == RT_NULL || fb->pixels == RT_NULL) return;

    /* 如果 DMA2D 可用, 走硬件加速; 否则回退到 CPU 路径 */
    extern int udisp_dma2d_is_ready(void);
    extern int udisp_dma2d_clear(udisp_fb_t *fb, uint16_t color_rgb565);
    if (udisp_dma2d_is_ready())
    {
        if (udisp_dma2d_clear(fb, color) == UDISP_OK) return;
        /* 失败则 fallthrough 到 CPU 路径 */
    }

    uint32_t total = fb->width * fb->height;
    uint16_t *p = fb->pixels;

    /* 小优化: 用 32bit 写入, 一次写两个像素 */
    uint32_t color32 = ((uint32_t)color << 16) | color;
    uint32_t *p32 = (uint32_t *)p;
    uint32_t n32 = total / 2;

    for (uint32_t i = 0; i < n32; i++)
    {
        p32[i] = color32;
    }

    if (total & 1)
    {
        p[total - 1] = color;
    }
}

void udisp_fb_cache_clean(udisp_fb_t *fb)
{
    if (fb == RT_NULL) return;
    SCB_CleanDCache_by_Addr((uint32_t *)fb->pixels, fb->size);
}
