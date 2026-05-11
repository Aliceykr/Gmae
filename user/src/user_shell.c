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
#include "user_display_dma2d.h"
#include "user_display_jpeg.h"
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

    rt_kprintf("USB Display Bridge\n");
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

    rt_kprintf("  dma2d         : %s\n", udisp_dma2d_is_ready() ? "ready" : "disabled");

    udisp_jpeg_stats_t js;
    udisp_jpeg_get_stats(&js);
    rt_kprintf("  jpeg frames   : %u\n", js.total_frames);
    if (js.total_frames)
    {
        rt_kprintf("  jpeg last     : convert=%u us, encode=%u us, bytes=%u\n",
                   js.last_convert_us, js.last_encode_us, js.last_jpeg_bytes);
    }

    rt_kprintf("  render frames : %u\n", s_frame_idx);
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


/* ------------------------------ JPEG commands ------------------------------ */

static int cmd_udisp_jpeg(int argc, char **argv)
{
    if (!udisp_is_ready())
    {
        rt_kprintf("Not initialized. Run 'udisp_init' first.\n");
        return -1;
    }

    /* 编码前台缓冲当前内容 */
    udisp_fb_t *fb = udisp_fb_get_front();
    if (fb == RT_NULL)
    {
        rt_kprintf("No front buffer.\n");
        return -1;
    }

    uint8_t  *out = udisp_jpeg_get_output_buffer();
    uint32_t  cap = udisp_jpeg_get_output_capacity();
    uint32_t  len = 0;

    int rc = udisp_jpeg_encode(fb, out, cap, &len);
    if (rc != UDISP_OK)
    {
        rt_kprintf("encode failed: %d\n", rc);
        return rc;
    }

    udisp_jpeg_stats_t js;
    udisp_jpeg_get_stats(&js);
    rt_kprintf("JPEG OK: %u bytes (convert=%u us, encode=%u us)\n",
               len, js.last_convert_us, js.last_encode_us);
    rt_kprintf("  compression   : %u : 1 (raw=%u -> jpeg=%u)\n",
               len ? (UDISP_FB_SIZE / len) : 0, UDISP_FB_SIZE, len);
    rt_kprintf("  output buffer : 0x%08x\n", (uint32_t)out);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_udisp_jpeg, udisp_jpeg, Encode front buffer to JPEG);

/* 组合: 渲染一帧测试图案并编码, 打印耗时 */
static int cmd_udisp_jpeg_test(int argc, char **argv)
{
    if (!udisp_is_ready())
    {
        rt_kprintf("Not initialized.\n");
        return -1;
    }

    udisp_pattern_t pat = UDISP_PATTERN_GRADIENT;
    if (argc >= 2)
    {
        int v = atoi(argv[1]);
        if (v >= 0 && v < UDISP_PATTERN_MAX) pat = (udisp_pattern_t)v;
    }

    udisp_fb_t *fb = udisp_fb_get_back();
    uint32_t t0 = tick_ms();
    udisp_draw_test_pattern(fb, pat, s_frame_idx++);
    udisp_fb_swap();
    uint32_t t_render = tick_ms() - t0;

    /* swap 后, 刚画的那一帧已经是 front buffer */
    fb = udisp_fb_get_front();
    uint8_t *out = udisp_jpeg_get_output_buffer();
    uint32_t cap = udisp_jpeg_get_output_capacity();
    uint32_t len = 0;

    t0 = tick_ms();
    int rc = udisp_jpeg_encode(fb, out, cap, &len);
    uint32_t t_jpeg = tick_ms() - t0;

    if (rc != UDISP_OK)
    {
        rt_kprintf("encode failed: %d\n", rc);
        return rc;
    }

    udisp_jpeg_stats_t js;
    udisp_jpeg_get_stats(&js);

    uint32_t t_total = t_render + t_jpeg;
    rt_kprintf("pattern=%d\n", pat);
    rt_kprintf("  render        : %u ms\n", t_render);
    rt_kprintf("  jpeg total    : %u ms (convert=%u us, encode=%u us)\n",
               t_jpeg, js.last_convert_us, js.last_encode_us);
    rt_kprintf("  jpeg size     : %u bytes (%u:1 compression)\n",
               len, len ? (UDISP_FB_SIZE / len) : 0);
    rt_kprintf("  pipeline      : %u ms/frame, ~%u fps max\n",
               t_total, t_total ? (1000 / t_total) : 0);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_udisp_jpeg_test, udisp_jpeg_test, Render+Encode benchmark);

/* 循环编码 N 帧, 统计 JPEG 单独耗时 */
static int cmd_udisp_jpeg_bench(int argc, char **argv)
{
    if (!udisp_is_ready())
    {
        rt_kprintf("Not initialized.\n");
        return -1;
    }

    int n = 10;
    if (argc >= 2) n = atoi(argv[1]);
    if (n < 1) n = 1;
    if (n > 100) n = 100;

    udisp_fb_t *fb = udisp_fb_get_back();
    /* 先画一次渐变, 后面只编码不重画 */
    udisp_draw_test_pattern(fb, UDISP_PATTERN_GRADIENT, 0);
    udisp_fb_swap();
    fb = udisp_fb_get_front();

    uint8_t *out = udisp_jpeg_get_output_buffer();
    uint32_t cap = udisp_jpeg_get_output_capacity();

    rt_kprintf("JPEG benchmark: %d frames (gradient, repeated)...\n", n);
    uint32_t total_convert = 0, total_encode = 0, total_bytes = 0;
    uint32_t t0 = tick_ms();
    for (int i = 0; i < n; i++)
    {
        uint32_t len = 0;
        if (udisp_jpeg_encode(fb, out, cap, &len) != UDISP_OK)
        {
            rt_kprintf("  iter %d failed\n", i);
            return -1;
        }
        udisp_jpeg_stats_t js;
        udisp_jpeg_get_stats(&js);
        total_convert += js.last_convert_us;
        total_encode  += js.last_encode_us;
        total_bytes   += len;
    }
    uint32_t dt = tick_ms() - t0;

    rt_kprintf("Total: %u ms, avg: %u ms/frame, ~%u fps\n",
               dt, (dt + n/2) / n,
               dt ? (n * 1000 / dt) : 0);
    rt_kprintf("  avg convert   : %u us\n", total_convert / n);
    rt_kprintf("  avg encode    : %u us\n", total_encode  / n);
    rt_kprintf("  avg jpeg size : %u bytes\n", total_bytes / n);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_udisp_jpeg_bench, udisp_jpeg_bench, Benchmark JPEG encoding);
