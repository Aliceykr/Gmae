/*
 * DMA2D 渲染加速实现
 *
 * Copyright (c) 2025
 */
#include <rtthread.h>
#include <string.h>

#include "user_display_dma2d.h"
#include "user_display.h"
#include "stm32h7rsxx_hal.h"

#define DBG_TAG "udisp.d2d"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ----------------------------- 状态 ----------------------------- */

static DMA2D_HandleTypeDef s_hdma2d;
static rt_bool_t s_inited = RT_FALSE;
static struct rt_mutex s_mtx;

/* ----------------------------- 底层 ----------------------------- */

int udisp_dma2d_init(void)
{
    if (s_inited) return UDISP_OK;

    __HAL_RCC_DMA2D_CLK_ENABLE();

    s_hdma2d.Instance = DMA2D;
    s_hdma2d.Init.Mode          = DMA2D_R2M;           /* Register to Memory, 填充模式 */
    s_hdma2d.Init.ColorMode     = DMA2D_OUTPUT_RGB565;
    s_hdma2d.Init.OutputOffset  = 0;
    s_hdma2d.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
    s_hdma2d.Init.RedBlueSwap   = DMA2D_RB_REGULAR;

    if (HAL_DMA2D_Init(&s_hdma2d) != HAL_OK)
    {
        LOG_E("HAL_DMA2D_Init failed");
        return UDISP_ERR;
    }

    rt_mutex_init(&s_mtx, "udisp_d2d", RT_IPC_FLAG_PRIO);
    s_inited = RT_TRUE;
    LOG_I("DMA2D ready");
    return UDISP_OK;
}

int udisp_dma2d_deinit(void)
{
    if (!s_inited) return UDISP_OK;
    HAL_DMA2D_DeInit(&s_hdma2d);
    __HAL_RCC_DMA2D_CLK_DISABLE();
    rt_mutex_detach(&s_mtx);
    s_inited = RT_FALSE;
    return UDISP_OK;
}

int udisp_dma2d_is_ready(void)
{
    return s_inited ? 1 : 0;
}

/* ----------------------------- clear ----------------------------- */

/* DMA2D 填充的颜色需要展开成 RGB888 的 0x00RRGGBB (Alpha 忽略)
 * R2M 模式会根据 ColorMode 自动下采样到 RGB565 输出 */
static uint32_t rgb565_to_rgb888_hex(uint16_t c)
{
    uint32_t r = (c >> 11) & 0x1F;
    uint32_t g = (c >> 5)  & 0x3F;
    uint32_t b =  c        & 0x1F;
    /* 5->8, 6->8 bit 扩展 */
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

int udisp_dma2d_clear(udisp_fb_t *fb, uint16_t color_rgb565)
{
    if (!s_inited || fb == RT_NULL) return UDISP_ERR_NOT_INIT;

    rt_mutex_take(&s_mtx, RT_WAITING_FOREVER);

    /* 配置为 R2M (register to memory) 填充 */
    s_hdma2d.Init.Mode         = DMA2D_R2M;
    s_hdma2d.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
    s_hdma2d.Init.OutputOffset = 0;
    HAL_DMA2D_Init(&s_hdma2d);

    uint32_t color = rgb565_to_rgb888_hex(color_rgb565);
    HAL_StatusTypeDef st = HAL_DMA2D_Start(&s_hdma2d, color,
                                           (uint32_t)fb->pixels,
                                           fb->width, fb->height);
    if (st == HAL_OK)
    {
        st = HAL_DMA2D_PollForTransfer(&s_hdma2d, 100);
    }

    rt_mutex_release(&s_mtx);
    return (st == HAL_OK) ? UDISP_OK : UDISP_ERR;
}

/* ----------------------------- fill_rect ----------------------------- */

int udisp_dma2d_fill_rect(udisp_fb_t *fb,
                          int x, int y, int w, int h,
                          uint16_t color_rgb565)
{
    if (!s_inited || fb == RT_NULL) return UDISP_ERR_NOT_INIT;
    if (w <= 0 || h <= 0) return UDISP_OK;

    /* 裁剪 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)fb->width || y >= (int)fb->height) return UDISP_OK;
    if (x + w > (int)fb->width)  w = fb->width  - x;
    if (y + h > (int)fb->height) h = fb->height - y;

    rt_mutex_take(&s_mtx, RT_WAITING_FOREVER);

    /* 目标 = fb + (y * stride + x * bpp) */
    uint32_t target = (uint32_t)fb->pixels + (y * fb->width + x) * UDISP_BPP;

    s_hdma2d.Init.Mode         = DMA2D_R2M;
    s_hdma2d.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
    /* OutputOffset 单位: 像素; 每写完一行需要跳过 (width - w) 个像素 */
    s_hdma2d.Init.OutputOffset = fb->width - w;
    HAL_DMA2D_Init(&s_hdma2d);

    uint32_t color = rgb565_to_rgb888_hex(color_rgb565);
    HAL_StatusTypeDef st = HAL_DMA2D_Start(&s_hdma2d, color, target,
                                           (uint32_t)w, (uint32_t)h);
    if (st == HAL_OK)
    {
        st = HAL_DMA2D_PollForTransfer(&s_hdma2d, 100);
    }

    rt_mutex_release(&s_mtx);
    return (st == HAL_OK) ? UDISP_OK : UDISP_ERR;
}
