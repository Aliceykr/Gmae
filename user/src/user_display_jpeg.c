/*
 * 硬件 JPEG 编码器 + 条带式流水线
 *
 * 核心思想:
 *   把 RGB565 帧缓冲分成 30 个 "MCU 行" (每行 16 像素高),
 *   每次只把一行 MCU (~19.2KB) 从 RGB565 转换成 YCbCr 放到 SRAM 条带缓冲,
 *   然后交给 JPEG 外设处理. JPEG 通过 GetDataCallback 自动请求下一条带.
 *
 * 优势:
 *   - MCU 条带驻留 SRAM, CPU 写入速度是 PSRAM 的 ~5-10 倍
 *   - JPEG 读条带也走 SRAM, 不占用 AHB5 带宽
 *   - convert (CPU) 和 encode (JPEG 外设) 天然并行 (两个条带轮换)
 *
 * Copyright (c) 2025
 */

#include <rtthread.h>
#include <string.h>

#include "user_display_jpeg.h"
#include "user_display.h"
#include "stm32h7rsxx_hal.h"
#include "stm32h7rsxx_hal_jpeg.h"

#define DBG_TAG "udisp.jpeg"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ----------------------------- 状态 ----------------------------- */

static JPEG_HandleTypeDef s_hjpeg;
static rt_bool_t s_inited = RT_FALSE;
static udisp_jpeg_stats_t s_stats;
static struct rt_mutex s_mtx;

/* 双条带 MCU 缓冲: 位于 SRAM (BSS), 32 字节对齐便于 cache/DMA */
static uint8_t s_stripe_pool[UDISP_JPEG_STRIPE_POOL_SIZE] __attribute__((aligned(32)));

/* JPEG 输出仍留在 PSRAM, 因为:
 *   - 它是唯一不参与编码热路径的大块内存
 *   - 压缩后只有 ~15KB/帧, 读写带宽不敏感 */
static uint8_t * const s_jpeg_out   = (uint8_t *)UDISP_JPEG_OUT_ADDR;
static const uint32_t  s_jpeg_out_cap = UDISP_JPEG_OUT_SIZE;

/* ----------------------------- 流水线上下文 ----------------------------- */

/* 一次 encode 调用期间, 以下状态在回调和主函数间共享 */
typedef struct
{
    const udisp_fb_t *fb;           /* 源帧缓冲 */
    uint32_t next_stripe_idx;       /* 下一个要转换的 MCU 行号 (0..29) */
    uint32_t active_buf;            /* 当前 JPEG 正在读取的条带索引 (0/1) */
    uint32_t encoded_bytes;         /* 已输出的 JPEG 字节数 (累加值) */
    volatile rt_bool_t encode_done; /* 编码完成标志 */
    volatile rt_err_t  encode_err;
    uint32_t total_convert_us;      /* 累计 CPU convert 时间 */
    uint8_t *out_buf;
    uint32_t out_cap;
} jpeg_pipe_ctx_t;

static jpeg_pipe_ctx_t s_ctx;

/* ----------------------------- DWT 计时 ----------------------------- */
static uint32_t s_cycles_per_us = 600;  /* 运行时计算, 默认 600MHz */

static inline void dwt_init(void)
{
    static int inited = 0;
    if (inited) return;

    /* 解锁 DWT (某些芯片 reset 后是锁的) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* 尝试解锁 ITM Lock Access Register (部分芯片需要) */
    DWT->LAR = 0xC5ACCE55;

    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    extern uint32_t SystemCoreClock;
    if (SystemCoreClock > 0)
    {
        s_cycles_per_us = (SystemCoreClock + 500000) / 1000000;
    }
    inited = 1;
}
static inline uint32_t dwt_get(void) { return DWT->CYCCNT; }
static inline uint32_t dwt_us(uint32_t start, uint32_t end)
{
    uint32_t cycles = end - start;   /* 32bit 自然处理回卷 */
    return cycles / s_cycles_per_us;
}

/* ----------------------------- RGB565 LUT ----------------------------- */

static uint8_t s_lut5[32];
static uint8_t s_lut6[64];
static int     s_lut_ready = 0;

static void lut_init(void)
{
    if (s_lut_ready) return;
    for (int i = 0; i < 32; i++) s_lut5[i] = (uint8_t)((i << 3) | (i >> 2));
    for (int i = 0; i < 64; i++) s_lut6[i] = (uint8_t)((i << 2) | (i >> 4));
    s_lut_ready = 1;
}

static inline uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* 把 fb 中 1 个 16x16 块转成 6 个 8x8 块, 写入 out 指向的 384 字节 */
static void convert_mcu(const uint16_t *fb, uint32_t stride_pix,
                        int mx, int my, uint8_t *out)
{
    uint8_t *y0 = out + 0 * 64;
    uint8_t *y1 = out + 1 * 64;
    uint8_t *y2 = out + 2 * 64;
    uint8_t *y3 = out + 3 * 64;
    uint8_t *cb = out + 4 * 64;
    uint8_t *cr = out + 5 * 64;

    for (int by = 0; by < 16; by++)
    {
        const uint16_t *row = fb + (my + by) * stride_pix + mx;
        for (int bx = 0; bx < 16; bx++)
        {
            uint16_t c = row[bx];
            uint8_t r = s_lut5[(c >> 11) & 0x1F];
            uint8_t g = s_lut6[(c >> 5)  & 0x3F];
            uint8_t b = s_lut5[ c        & 0x1F];

            int Y = (19595 * r + 38470 * g + 7471 * b) >> 16;

            uint8_t *yblk;
            int bxi = bx & 7, byi = by & 7;
            if (by < 8) yblk = (bx < 8) ? y0 : y1;
            else        yblk = (bx < 8) ? y2 : y3;
            yblk[byi * 8 + bxi] = clamp_u8(Y);

            if (!(bx & 1) && !(by & 1))
            {
                int Cb = ((-11056 * r - 21712 * g + 32768 * b) >> 16) + 128;
                int Cr = (( 32768 * r - 27440 * g - 5328  * b) >> 16) + 128;
                int cx = bx >> 1, cy = by >> 1;
                cb[cy * 8 + cx] = clamp_u8(Cb);
                cr[cy * 8 + cx] = clamp_u8(Cr);
            }
        }
    }
}

/* 把 fb 中一整行 MCU (16 像素高 × 800 像素宽) 转到 stripe 缓冲 */
static uint32_t convert_mcu_row(const udisp_fb_t *fb, uint32_t row_idx, uint8_t *stripe)
{
    uint32_t t0 = dwt_get();
    int my = row_idx * UDISP_JPEG_MCU_HEIGHT;
    for (int mx_i = 0; mx_i < UDISP_JPEG_MCU_COUNT_X; mx_i++)
    {
        uint8_t *dst = stripe + mx_i * UDISP_JPEG_MCU_BYTES;
        int mx = mx_i * UDISP_JPEG_MCU_WIDTH;
        convert_mcu(fb->pixels, fb->width, mx, my, dst);
    }
    uint32_t t1 = dwt_get();
    return dwt_us(t0, t1);
}

/* ----------------------------- MSP (HAL 钩子) ----------------------------- */

void HAL_JPEG_MspInit(JPEG_HandleTypeDef *hjpeg)
{
    (void)hjpeg;
    /* 时钟由 udisp_jpeg_init 统一管理 */

    /* IT 模式需要 NVIC */
    HAL_NVIC_SetPriority(JPEG_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(JPEG_IRQn);
}

void HAL_JPEG_MspDeInit(JPEG_HandleTypeDef *hjpeg)
{
    (void)hjpeg;
    HAL_NVIC_DisableIRQ(JPEG_IRQn);
    __HAL_RCC_JPEG_CLK_DISABLE();
}

/* ----------------------------- HAL JPEG 回调 ----------------------------- *
 *
 * HAL 会通过这些回调驱动整个编码过程.
 * 我们只用 _IT (中断) 模式, JPEG 外设会自己去读我们指定的 MCU 条带,
 * 读完触发 GetDataCallback 问我们要下一条带, 输出缓冲满时触发 DataReadyCallback.
 */

/* JPEG 已消费完当前输入, 要下一批 */
void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t NbEncodedData)
{
    /* 注意: 这里是中断上下文! */
    (void)NbEncodedData;

    if (s_ctx.next_stripe_idx >= UDISP_JPEG_MCU_COUNT_Y)
    {
        /* 所有条带已提交, 通知 HAL 输入结束 */
        HAL_JPEG_ConfigInputBuffer(hjpeg, NULL, 0);
        return;
    }

    /* 切换到另一个条带 buffer 填入下一行数据.
     * 但这里是中断上下文, 不能做 CPU convert (太耗时).
     * 策略: 回调里切 buffer 让 JPEG 继续消费当前 buffer 剩余的 MCU (如果还有),
     *       下一行的 convert 在主循环里异步完成.
     *
     * 这里我们采用更简单的方式: 在回调中同步完成 convert. 因为 JPEG 编码速度
     * (~1 us/MCU) 和 CPU convert 速度 (~20 us/MCU) 差距大, 即使 convert
     * 阻塞在中断里, 总时间也不会更糟 (JPEG 会自然等待).
     *
     * 要做真正的流水线需要在主循环里准备好 *下一条带*, 回调里只切指针.
     * 先做最简单版本验证收益. */

    uint32_t buf_idx = s_ctx.active_buf ^ 1;   /* 换到另一块 */
    uint8_t *stripe  = s_stripe_pool + buf_idx * UDISP_JPEG_STRIPE_BYTES;

    /* 转换 next_stripe_idx 行到 stripe */
    uint32_t us = convert_mcu_row(s_ctx.fb, s_ctx.next_stripe_idx, stripe);
    s_ctx.total_convert_us += us;
    s_ctx.next_stripe_idx++;

    /* clean cache 让 JPEG 外设读到最新数据 (SRAM 也启用了 cache) */
    SCB_CleanDCache_by_Addr((uint32_t *)stripe, UDISP_JPEG_STRIPE_BYTES);

    /* 喂给 JPEG */
    HAL_JPEG_ConfigInputBuffer(hjpeg, stripe, UDISP_JPEG_STRIPE_BYTES);
    s_ctx.active_buf = buf_idx;
}

/* JPEG 输出缓冲已满或编码结束 */
void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg,
                                 uint8_t *pDataOut, uint32_t OutDataLength)
{
    (void)pDataOut;
    s_ctx.encoded_bytes += OutDataLength;

    /* 把输出窗口往前推 */
    uint32_t used = s_ctx.encoded_bytes;
    if (used < s_ctx.out_cap)
    {
        HAL_JPEG_ConfigOutputBuffer(hjpeg,
                                    s_ctx.out_buf + used,
                                    s_ctx.out_cap - used);
    }
    else
    {
        /* 空间不足 */
        HAL_JPEG_ConfigOutputBuffer(hjpeg, s_ctx.out_buf, 0);
    }
}

void HAL_JPEG_EncodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
    (void)hjpeg;
    s_ctx.encode_err = RT_EOK;
    s_ctx.encode_done = RT_TRUE;
}

void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg)
{
    uint32_t err = HAL_JPEG_GetError(hjpeg);
    LOG_E("JPEG error 0x%x", err);
    s_ctx.encode_err  = -RT_ERROR;
    s_ctx.encode_done = RT_TRUE;
}

/* JPEG IRQHandler (覆盖 startup 里的 weak 符号) */
void JPEG_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_JPEG_IRQHandler(&s_hjpeg);
    rt_interrupt_leave();
}

/* ----------------------------- 初始化 ----------------------------- */

int udisp_jpeg_init(void)
{
    if (s_inited) return UDISP_OK;

    dwt_init();
    lut_init();

    __HAL_RCC_JPEG_CLK_ENABLE();

    rt_memset(&s_hjpeg, 0, sizeof(s_hjpeg));
    s_hjpeg.Instance = JPEG;
    if (HAL_JPEG_Init(&s_hjpeg) != HAL_OK)
    {
        LOG_E("HAL_JPEG_Init failed");
        return UDISP_ERR;
    }

    rt_mutex_init(&s_mtx, "udisp_jpg", RT_IPC_FLAG_PRIO);
    rt_memset(&s_stats, 0, sizeof(s_stats));

    s_inited = RT_TRUE;
    {
        extern uint32_t SystemCoreClock;
        LOG_I("SystemCoreClock = %u Hz, DWT %u cycles/us",
              SystemCoreClock, s_cycles_per_us);
    }
    LOG_I("JPEG ready (IT/stripe mode), stripe=%u B x %u in SRAM, out @0x%08x (%u KB)",
          UDISP_JPEG_STRIPE_BYTES, UDISP_JPEG_STRIPE_COUNT,
          (uint32_t)s_jpeg_out, s_jpeg_out_cap / 1024);
    LOG_I("MCU: %dx%d (%d rows of %d MCUs)",
          UDISP_JPEG_MCU_COUNT_X, UDISP_JPEG_MCU_COUNT_Y,
          UDISP_JPEG_MCU_COUNT_Y, UDISP_JPEG_MCU_COUNT_X);
    return UDISP_OK;
}

int udisp_jpeg_deinit(void)
{
    if (!s_inited) return UDISP_OK;
    HAL_JPEG_DeInit(&s_hjpeg);
    rt_mutex_detach(&s_mtx);
    s_inited = RT_FALSE;
    return UDISP_OK;
}

uint8_t  *udisp_jpeg_get_output_buffer(void)    { return s_jpeg_out; }
uint32_t  udisp_jpeg_get_output_capacity(void)  { return s_jpeg_out_cap; }

void udisp_jpeg_get_stats(udisp_jpeg_stats_t *stats)
{
    if (stats) *stats = s_stats;
}

/* ----------------------------- 编码主流程 ----------------------------- */

int udisp_jpeg_encode(const udisp_fb_t *fb,
                      uint8_t *out_buf, uint32_t out_cap,
                      uint32_t *out_len)
{
    if (!s_inited) return UDISP_ERR_NOT_INIT;
    if (!fb || !out_buf || !out_len) return UDISP_ERR_INVAL;

    rt_mutex_take(&s_mtx, RT_WAITING_FOREVER);

    *out_len = 0;

    /* 额外的基于 tick 的测时 (ms 精度), 用于和 DWT 交叉验证 */
    uint32_t tick_start = rt_tick_get_millisecond();

    /* 1) 配置 JPEG 编码参数 */
    JPEG_ConfTypeDef conf;
    conf.ColorSpace        = JPEG_YCBCR_COLORSPACE;
    conf.ChromaSubsampling = JPEG_420_SUBSAMPLING;
    conf.ImageHeight       = UDISP_HEIGHT;
    conf.ImageWidth        = UDISP_WIDTH;
    conf.ImageQuality      = UDISP_JPEG_QUALITY;

    if (HAL_JPEG_ConfigEncoding(&s_hjpeg, &conf) != HAL_OK)
    {
        LOG_E("HAL_JPEG_ConfigEncoding failed");
        rt_mutex_release(&s_mtx);
        return UDISP_ERR;
    }

    /* 2) 预填充第一条带 */
    s_ctx.fb               = fb;
    s_ctx.next_stripe_idx  = 0;
    s_ctx.active_buf       = 0;
    s_ctx.encoded_bytes    = 0;
    s_ctx.encode_done      = RT_FALSE;
    s_ctx.encode_err       = RT_EOK;
    s_ctx.total_convert_us = 0;
    s_ctx.out_buf          = out_buf;
    s_ctx.out_cap          = out_cap;

    uint32_t tt0 = dwt_get();

    /* 先转第 0 行 */
    uint32_t us = convert_mcu_row(fb, 0, s_stripe_pool);
    s_ctx.total_convert_us += us;
    s_ctx.next_stripe_idx = 1;
    SCB_CleanDCache_by_Addr((uint32_t *)s_stripe_pool, UDISP_JPEG_STRIPE_BYTES);

    /* 3) 启动 IT 模式编码, 提供第 0 条带作为初始输入 */
    HAL_StatusTypeDef st = HAL_JPEG_Encode_IT(&s_hjpeg,
                                              s_stripe_pool, UDISP_JPEG_STRIPE_BYTES,
                                              out_buf, out_cap);
    if (st != HAL_OK)
    {
        LOG_E("HAL_JPEG_Encode_IT start failed, st=%d", st);
        rt_mutex_release(&s_mtx);
        return UDISP_ERR;
    }

    /* 4) 等待完成. 因为回调里做的 convert 会阻塞中断上下文较长,
     *    我们主循环只是轮询等, 加超时保护. */
    uint32_t start_tick = rt_tick_get();
    const uint32_t TIMEOUT_TICKS = rt_tick_from_millisecond(2000);
    while (!s_ctx.encode_done)
    {
        if ((rt_tick_get() - start_tick) > TIMEOUT_TICKS)
        {
            LOG_E("JPEG encode timeout");
            HAL_JPEG_Abort(&s_hjpeg);
            rt_mutex_release(&s_mtx);
            return UDISP_ERR;
        }
        rt_thread_yield();
    }

    uint32_t tt1 = dwt_get();

    if (s_ctx.encode_err != RT_EOK)
    {
        rt_mutex_release(&s_mtx);
        return UDISP_ERR;
    }

    *out_len = s_ctx.encoded_bytes;
    s_stats.last_convert_us = s_ctx.total_convert_us;
    s_stats.last_encode_us  = dwt_us(tt0, tt1);     /* 整个 pipeline 总耗时 */
    s_stats.last_jpeg_bytes = s_ctx.encoded_bytes;
    s_stats.total_frames++;
    s_stats.last_tick_ms    = rt_tick_get_millisecond() - tick_start;
    s_stats.last_stripe_cnt = s_ctx.next_stripe_idx;

    rt_mutex_release(&s_mtx);
    return UDISP_OK;
}
