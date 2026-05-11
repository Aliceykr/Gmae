/*
 * 硬件 JPEG 编码器 + RGB565 -> YCbCr 4:2:0 MCU 转换
 *
 * 4:2:0 MCU 布局 (每个 MCU 覆盖 16x16 像素, 共 6 个 8x8 块):
 *   [ Y0  (8x8) | Y1  (8x8)  ]    <-  左上 / 右上 亮度
 *   [ Y2  (8x8) | Y3  (8x8)  ]    <-  左下 / 右下 亮度
 *   [ Cb  (8x8) ]                 <-  16x16 降采样到 8x8
 *   [ Cr  (8x8) ]
 *
 * HAL_JPEG 要求输入格式:
 *   按 MCU 顺序排列, 每 MCU 384 字节 (4 Y + 1 Cb + 1 Cr)
 *   块内按行存储 (8 行 x 8 字节)
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

/* MCU 缓冲 / JPEG 输出缓冲 都预分配在 PSRAM */
static uint8_t * const s_mcu_buf    = (uint8_t *)UDISP_MCU_IN_ADDR;
static uint8_t * const s_jpeg_out   = (uint8_t *)UDISP_JPEG_OUT_ADDR;
static const uint32_t  s_jpeg_out_cap = UDISP_JPEG_OUT_SIZE;

/* HAL_JPEG 通过 DataReadyCallback 回调来告知已输出的数据块.
 * 由于我们在 HAL_JPEG_Encode 之前配置了单一大输出缓冲(256KB), 
 * 正常情况下 DataReadyCallback 只在编码结束时被调用一次,
 * 此时 len 就是最终 JPEG 字节数. */
static volatile uint32_t s_encoded_bytes;

/* ----------------------------- cycle counter ----------------------------- */
/* DWT 精确计时, 用于统计 us 级耗时 */
static inline void dwt_init(void)
{
    static int inited = 0;
    if (inited) return;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    inited = 1;
}

static inline uint32_t dwt_get(void) { return DWT->CYCCNT; }

static inline uint32_t dwt_us(uint32_t start, uint32_t end)
{
    /* 假设 600 MHz. 如果主频变更, 这里需要同步修改. */
    uint32_t cycles = end - start;
    return cycles / 600;
}

/* ----------------------------- MSP (HAL 钩子) ----------------------------- */

/* HAL 库会在 HAL_JPEG_Init 内部调用 HAL_JPEG_MspInit,
 * 由用户提供时钟和中断配置. 由于我们已经在 udisp_jpeg_init 中手动开了时钟,
 * 这里不再重复 enable (避免 RCC 竞态). */
void HAL_JPEG_MspInit(JPEG_HandleTypeDef *hjpeg)
{
    (void)hjpeg;
    /* 时钟由 udisp_jpeg_init 统一管理, 此处为空. */
}

void HAL_JPEG_MspDeInit(JPEG_HandleTypeDef *hjpeg)
{
    if (hjpeg->Instance == JPEG)
    {
        __HAL_RCC_JPEG_CLK_DISABLE();
    }
}

/* ----------------------------- JPEG 回调 ----------------------------- *
 * 阻塞式 HAL_JPEG_Encode 内部会循环调用这些回调来喂数据/收数据.
 *
 * GetDataCallback(hjpeg, NbEncodedData): 编码器已消费 NbEncodedData 字节,
 *   我们要调用 HAL_JPEG_ConfigInputBuffer 喂下一批.
 *   由于我们一次性把整个 MCU 缓冲提供给 HAL (InDataLength=576000),
 *   这个回调 NbEncodedData==InDataLength 时表示输入耗尽, 停止输入.
 *
 * DataReadyCallback(hjpeg, pDataOut, OutDataLength): 输出缓冲已满或编码结束,
 *   我们把它累加到 s_encoded_bytes, 然后再给一个新的输出缓冲继续接.
 *   为了简单起见, 我们只用一个大输出缓冲, 并假设不会溢出 (实际 800x480@Q80 << 256KB).
 *   如果真溢出, 可以用 HAL_JPEG_ConfigOutputBuffer 继续.
 */

void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t NbEncodedData)
{
    /* 输入已经一次性提供完毕; NbEncodedData 代表已消费的字节数.
     * 如果还没消费完, HAL 会继续自动从 pJpegInBuffPtr 读取;
     * 如果消费完了, 调用 HAL_JPEG_ConfigInputBuffer(NULL, 0) 告知结束. */
    if (NbEncodedData >= hjpeg->InDataLength)
    {
        HAL_JPEG_ConfigInputBuffer(hjpeg, NULL, 0);
    }
    else
    {
        /* 继续使用剩余部分 (理论上不会进到这里, 因为我们一次性给了全部输入) */
        HAL_JPEG_ConfigInputBuffer(hjpeg,
                                   hjpeg->pJpegInBuffPtr + NbEncodedData,
                                   hjpeg->InDataLength   - NbEncodedData);
    }
}

void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg,
                                 uint8_t *pDataOut, uint32_t OutDataLength)
{
    (void)pDataOut;
    /* 累加实际编码输出的字节数. */
    s_encoded_bytes += OutDataLength;

    /* 继续提供后续输出缓冲.
     * 我们把整个 s_jpeg_out (256KB) 分成多次使用其实也 OK, 但更简单:
     * 每次都把接下来的空间接上. 若剩余空间不足则提供 0 让 HAL 报错. */
    uint32_t used = s_encoded_bytes;
    if (used < s_jpeg_out_cap)
    {
        HAL_JPEG_ConfigOutputBuffer(hjpeg,
                                    s_jpeg_out + used,
                                    s_jpeg_out_cap - used);
    }
    else
    {
        HAL_JPEG_ConfigOutputBuffer(hjpeg, s_jpeg_out, 0);
    }
}

/* ----------------------------- 初始化 ----------------------------- */

int udisp_jpeg_init(void)
{
    if (s_inited) return UDISP_OK;

    dwt_init();

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
    LOG_I("JPEG encoder ready, mcu_buf=0x%08x (%u bytes), out_buf=0x%08x (%u bytes)",
          (uint32_t)s_mcu_buf, UDISP_JPEG_MCU_IN_SIZE,
          (uint32_t)s_jpeg_out, s_jpeg_out_cap);
    LOG_I("MCU: %dx%d blocks of 16x16, total=%d", UDISP_JPEG_MCU_COUNT_X,
          UDISP_JPEG_MCU_COUNT_Y, UDISP_JPEG_MCU_TOTAL);
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

uint8_t  *udisp_jpeg_get_mcu_buffer(void)       { return s_mcu_buf; }
uint8_t  *udisp_jpeg_get_output_buffer(void)    { return s_jpeg_out; }
uint32_t  udisp_jpeg_get_output_capacity(void)  { return s_jpeg_out_cap; }

void udisp_jpeg_get_stats(udisp_jpeg_stats_t *stats)
{
    if (stats) *stats = s_stats;
}

/* ----------------------------- RGB565 -> YCbCr ----------------------------- *
 * 使用整数近似的 BT.601:
 *   Y  =  0.299 R + 0.587 G + 0.114 B
 *   Cb = -0.169 R - 0.331 G + 0.500 B + 128
 *   Cr =  0.500 R - 0.419 G - 0.081 B + 128
 * 乘法因子 << 16 转定点:
 *   Y  = (19595*R + 38470*G + 7471*B) >> 16
 *   Cb = (-11056*R - 21712*G + 32768*B) >> 16 + 128
 *   Cr = (32768*R - 27440*G - 5328*B) >> 16 + 128
 *
 * RGB565 展开为 8bit 的表 (预计算避免每次做移位+或运算)
 */

/* 预计算 5bit / 6bit 到 8bit 的展开 LUT */
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

static inline void rgb565_to_888(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = s_lut5[(c >> 11) & 0x1F];
    *g = s_lut6[(c >> 5)  & 0x3F];
    *b = s_lut5[ c        & 0x1F];
}

static inline uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* 把 fb 中一个 16x16 的像素块转为 6 个 8x8 块 (Y0,Y1,Y2,Y3,Cb,Cr)
 * 写入 out 指向的 384 字节 MCU 缓冲. */
static void convert_mcu(const uint16_t *fb, uint32_t fb_stride_pixels,
                        int mcu_x_pix, int mcu_y_pix,
                        uint8_t *out)
{
    uint8_t *y0 = out + 0   * 64;
    uint8_t *y1 = out + 1   * 64;
    uint8_t *y2 = out + 2   * 64;
    uint8_t *y3 = out + 3   * 64;
    uint8_t *cb = out + 4   * 64;
    uint8_t *cr = out + 5   * 64;

    /* 用于 4:2:0 下采样: 每 2x2 像素只产生 1 个 Cb/Cr,
     * 我们累加 4 个像素的 Cb/Cr 然后 >>2 (简单平均). */

    for (int by = 0; by < 16; by++)
    {
        const uint16_t *row = fb + (mcu_y_pix + by) * fb_stride_pixels + mcu_x_pix;
        for (int bx = 0; bx < 16; bx++)
        {
            uint8_t r, g, b;
            rgb565_to_888(row[bx], &r, &g, &b);

            /* Y */
            int Y  = (19595 * r + 38470 * g + 7471  * b) >> 16;

            /* 决定这个 Y 落在哪个 8x8 块 */
            uint8_t *y_blk;
            int bx_in = bx & 7;
            int by_in = by & 7;
            if (by < 8) y_blk = (bx < 8) ? y0 : y1;
            else        y_blk = (bx < 8) ? y2 : y3;
            y_blk[by_in * 8 + bx_in] = clamp_u8(Y);

            /* Cb / Cr 每 2x2 采样一次, 这里用左上角的那个像素作为该组的代表
             * (更准确的做法是 4 像素平均, 但对视觉效果影响很小且更慢) */
            if (!(bx & 1) && !(by & 1))
            {
                int Cb = ((-11056 * r - 21712 * g + 32768 * b) >> 16) + 128;
                int Cr = (( 32768 * r - 27440 * g - 5328  * b) >> 16) + 128;
                int cx = bx >> 1;       /* 0..7 */
                int cy = by >> 1;       /* 0..7 */
                cb[cy * 8 + cx] = clamp_u8(Cb);
                cr[cy * 8 + cx] = clamp_u8(Cr);
            }
        }
    }
}

int udisp_jpeg_fb_to_mcu(const udisp_fb_t *fb, uint8_t *mcu_buf)
{
    if (fb == RT_NULL || mcu_buf == RT_NULL) return UDISP_ERR_INVAL;
    if (fb->width != UDISP_WIDTH || fb->height != UDISP_HEIGHT) return UDISP_ERR_INVAL;

    lut_init();

    for (int my = 0; my < UDISP_JPEG_MCU_COUNT_Y; my++)
    {
        for (int mx = 0; mx < UDISP_JPEG_MCU_COUNT_X; mx++)
        {
            int mcu_idx = my * UDISP_JPEG_MCU_COUNT_X + mx;
            uint8_t *dst = mcu_buf + mcu_idx * UDISP_JPEG_MCU_BYTES;
            convert_mcu(fb->pixels, fb->width,
                        mx * UDISP_JPEG_MCU_WIDTH,
                        my * UDISP_JPEG_MCU_HEIGHT,
                        dst);
        }
    }
    return UDISP_OK;
}

/* ----------------------------- 编码入口 ----------------------------- */

int udisp_jpeg_encode(const udisp_fb_t *fb,
                      uint8_t *out_buf, uint32_t out_cap,
                      uint32_t *out_len)
{
    if (!s_inited) return UDISP_ERR_NOT_INIT;
    if (fb == RT_NULL || out_buf == RT_NULL || out_len == RT_NULL) return UDISP_ERR_INVAL;

    rt_mutex_take(&s_mtx, RT_WAITING_FOREVER);

    *out_len = 0;

    /* 1. 转换 RGB565 -> 4:2:0 MCU 块 */
    uint32_t t0 = dwt_get();
    int rc = udisp_jpeg_fb_to_mcu(fb, s_mcu_buf);
    if (rc != UDISP_OK)
    {
        rt_mutex_release(&s_mtx);
        return rc;
    }
    /* CPU 写完 MCU 缓冲, 需要 clean cache 让 JPEG 外设读到最新数据 */
    SCB_CleanDCache_by_Addr((uint32_t *)s_mcu_buf, UDISP_JPEG_MCU_IN_SIZE);
    uint32_t t1 = dwt_get();
    s_stats.last_convert_us = dwt_us(t0, t1);

    /* 2. 配置 JPEG 编码参数 */
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

    /* 3. 阻塞式编码. timeout 给 500ms (实际远不需要这么久) */
    s_encoded_bytes = 0;
    t0 = dwt_get();
    HAL_StatusTypeDef st = HAL_JPEG_Encode(&s_hjpeg,
                                           s_mcu_buf, UDISP_JPEG_MCU_IN_SIZE,
                                           out_buf, out_cap,
                                           500);
    t1 = dwt_get();
    s_stats.last_encode_us = dwt_us(t0, t1);

    if (st != HAL_OK)
    {
        LOG_E("HAL_JPEG_Encode failed, state=%d, error=0x%x",
              HAL_JPEG_GetState(&s_hjpeg), HAL_JPEG_GetError(&s_hjpeg));
        rt_mutex_release(&s_mtx);
        return UDISP_ERR;
    }

    /* 4. 回调已累加实际输出字节数 */
    *out_len = s_encoded_bytes;

    s_stats.last_jpeg_bytes = s_encoded_bytes;
    s_stats.total_frames++;

    rt_mutex_release(&s_mtx);
    return UDISP_OK;
}
