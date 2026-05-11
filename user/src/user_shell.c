/*
 * msh 调试命令
 *
 *   udisp_init            初始化显示桥 (帧缓冲 + PSRAM 自检)
 *   udisp_test [pattern]  在后台缓冲画一幅测试图案, 自动 swap
 *                         pattern: 0=solid 1=bars 2=checker 3=gradient 4=cross 5=anim
 *   udisp_stat            打印状态 / 上次渲染耗时
 *   udisp_bench [N]       连续渲染 N 帧, 统计平均耗时 (默认 30)
 *
 * Copyright (c) 2025
 */
#include <rtthread.h>
#include <stdlib.h>
#include <finsh.h>

#include "user_display.h"
#include "user_display_fb.h"
#include "user_test_pattern.h"

#define DBG_TAG "udisp.sh"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static uint32_t s_frame_idx   = 0;
static uint32_t s_last_ms     = 0;
static uint32_t s_bench_avg_ms = 0;

/* ms 级 tick helper */
static inline uint32_t tick_ms(void)
{
    return rt_tick_get_millisecond();
}

/* ------------------------------ commands ------------------------------ */

static int cmd_udisp_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    int rc = udisp_init();
    rt_kprintf("udisp_init: %s (%d)\n", (rc == UDISP_OK) ? "OK" : "FAIL", rc);
    return rc;
}
MSH_CMD_EXPORT_ALIAS(cmd_udisp_init, udisp_init, Init USB display bridge);

static int cmd_udisp_test(int argc, char **argv)
{
    if (!udisp_is_ready())
    {
        rt_kprintf("Not initialized. Run 'udisp_init' first.\n");
        return -1;
    }

    udisp_pattern_t pat = UDISP_PATTERN_CROSS;
    if (argc >= 2)
    {
        int v = atoi(argv[1]);
        if (v < 0 || v >= UDISP_PATTERN_MAX)
        {
            rt_kprintf("pattern 0..%d\n", UDISP_PATTERN_MAX - 1);
            return -1;
        }
        pat = (udisp_pattern_t)v;
    }

    udisp_fb_t *fb = udisp_fb_get_back();
    if (fb == RT_NULL)
    {
        rt_kprintf("no back buffer\n");
        return -1;
    }

    uint32_t t0 = tick_ms();
    udisp_draw_test_pattern(fb, pat, s_frame_idx++);
    udisp_fb_swap();
    s_last_ms = tick_ms() - t0;

    rt_kprintf("pattern=%d frame=%u render=%u ms (fb@0x%08x)\n",
               pat, s_frame_idx, s_last_ms, (uint32_t)fb->pixels);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_udisp_test, udisp_test, Render a test pattern);

static int cmd_udisp_stat(int argc, char **argv)
{
    (void)argc; (void)argv;

    rt_kprintf("USB Display Bridge - Phase 1\n");
    rt_kprintf("  ready         : %s\n", udisp_is_ready() ? "yes" : "no");
    rt_kprintf("  resolution    : %d x %d RGB565 (%u KB/frame)\n",
               UDISP_WIDTH, UDISP_HEIGHT, UDISP_FB_SIZE / 1024);

    udisp_fb_t *fb_b = udisp_fb_get_back();
    udisp_fb_t *fb_f = udisp_fb_get_front();
    if (fb_b && fb_f)
    {
        rt_kprintf("  back buffer   : idx=%d @0x%08x\n", fb_b->index, (uint32_t)fb_b->pixels);
        rt_kprintf("  front buffer  : idx=%d @0x%08x\n", fb_f->index, (uint32_t)fb_f->pixels);
    }

    rt_kprintf("  frame count   : %u\n", s_frame_idx);
    rt_kprintf("  last render   : %u ms\n", s_last_ms);
    if (s_bench_avg_ms)
        rt_kprintf("  bench avg     : %u ms/frame (~%u fps)\n",
                   s_bench_avg_ms, s_bench_avg_ms ? (1000 / s_bench_avg_ms) : 0);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_udisp_stat, udisp_stat, Show display bridge status);

static int cmd_udisp_bench(int argc, char **argv)
{
    if (!udisp_is_ready())
    {
        rt_kprintf("Not initialized. Run 'udisp_init' first.\n");
        return -1;
    }

    int n = 30;
    if (argc >= 2) n = atoi(argv[1]);
    if (n < 1) n = 1;
    if (n > 300) n = 300;

    udisp_fb_t *fb = udisp_fb_get_back();

    rt_kprintf("Benchmarking %d frames (gradient pattern, CPU render)...\n", n);
    uint32_t t0 = tick_ms();
    for (int i = 0; i < n; i++)
    {
        udisp_draw_test_pattern(fb, UDISP_PATTERN_GRADIENT, s_frame_idx++);
        udisp_fb_swap();
        fb = udisp_fb_get_back();  /* swap 之后后台缓冲变了 */
    }
    uint32_t dt = tick_ms() - t0;
    s_bench_avg_ms = (dt + n / 2) / n;

    rt_kprintf("Total: %u ms, avg: %u ms/frame, ~%u fps\n",
               dt, s_bench_avg_ms,
               s_bench_avg_ms ? (1000 / s_bench_avg_ms) : 0);
    rt_kprintf("(Note: CPU-only rendering on PSRAM is slow; JPEG path will use DMA2D later)\n");
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_udisp_bench, udisp_bench, Benchmark CPU rendering);
