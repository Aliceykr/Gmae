/*
 * 测试图案生成器
 *   在帧缓冲上画各种测试画面, 用于调试显示链路
 *
 * Copyright (c) 2025
 */
#ifndef __USER_TEST_PATTERN_H__
#define __USER_TEST_PATTERN_H__

#include "user_display_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UDISP_PATTERN_SOLID = 0,    /* 纯色 */
    UDISP_PATTERN_BARS,         /* 彩条 */
    UDISP_PATTERN_CHECKER,      /* 棋盘格 */
    UDISP_PATTERN_GRADIENT,     /* 渐变 */
    UDISP_PATTERN_CROSS,        /* 十字线 + 边框 */
    UDISP_PATTERN_ANIM_BAR,     /* 移动的竖条 (依赖 frame_idx) */
    UDISP_PATTERN_MAX
} udisp_pattern_t;

/**
 * @brief 在指定帧缓冲上渲染一种测试图案
 * @param fb         目标帧缓冲
 * @param pattern    图案类型
 * @param frame_idx  帧序号 (用于动画图案)
 * @return UDISP_OK / 负错误码
 */
int udisp_draw_test_pattern(udisp_fb_t *fb, udisp_pattern_t pattern, uint32_t frame_idx);

#ifdef __cplusplus
}
#endif

#endif /* __USER_TEST_PATTERN_H__ */
