# USB 显示桥 (USB HS Display Bridge)

把板子的帧缓冲通过 USB HS Bulk + 硬件 JPEG 编码传到 PC 端窗口显示。
目标 60fps @ 800x480 RGB565。

## 架构

```
 用户渲染/LVGL/测试图案
         ↓
    帧缓冲 (PSRAM, 双缓冲, 750KB×2)
         ↓
    DMA2D (硬件填充/搬运)           ← Phase 2
         ↓
    RGB565 → YCbCr 4:2:0 (CPU)     ← Phase 2, 待优化
         ↓
    硬件 JPEG 编码器 (H7RS 内置)    ← Phase 2
         ↓
    USB OTG HS Bulk IN 端点         ← Phase 3
         ↓
    PC 接收程序 (Python + SDL2)     ← Phase 3
         ↓
    PC 窗口
```

## 目录结构

```
user/
├── inc/
│   ├── user_display.h           主控 API
│   ├── user_display_fb.h        帧缓冲
│   ├── user_display_dma2d.h     DMA2D 加速
│   ├── user_test_pattern.h      测试图案
│   ├── user_display_jpeg.h      JPEG 编码
│   └── user_display_usb.h       USB Bulk (Phase 3)
├── src/
│   ├── user_display.c           主控 + 子系统装配
│   ├── user_display_fb.c        帧缓冲 + PSRAM 自检
│   ├── user_display_dma2d.c     DMA2D 硬件填充
│   ├── user_display_jpeg.c      HW JPEG + RGB565→YCbCr
│   ├── user_display_usb.c       stub (Phase 3)
│   ├── user_test_pattern.c      6 种测试图案
│   └── user_shell.c             msh 命令
├── pc_viewer/                   PC 端查看器 (Phase 3)
├── Kconfig
├── SConscript
└── README.md
```

## 实现阶段

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | 帧缓冲 + 测试图案 + 基础框架 | ✅ 完成 |
| Phase 2 | DMA2D 加速 + 硬件 JPEG 编码器 (polling) | ✅ 完成 |
| Phase 2.5 | JPEG 路径性能优化 (SRAM 条带 + IT 流水线) | ✅ 完成 |
| Phase 3 | USB HS Bulk + PC 查看器 | ⏳ 下一步 |

## Msh 命令

```
udisp_init                 # 初始化子系统 (main 启动时已自动调用)
udisp_stat                 # 查看状态 / 统计
udisp_test [0..5]          # 渲染一帧测试图案
                           # 0=solid 1=bars 2=checker 3=gradient 4=cross 5=anim
udisp_bench [N]            # 重复 N 次 gradient 渲染, 打印 fps
udisp_jpeg                 # 把 front buffer 编码为 JPEG
udisp_jpeg_test [pat]      # 渲染 + 编码组合测试
udisp_jpeg_bench [N]       # 循环编码 N 次, 统计耗时
```

## 内存布局 (PSRAM @ 0x90000000, 32MB)

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| FB0 | 0x90000000 | 1 MB (实用 750 KB) | 帧缓冲 0 (RGB565) |
| FB1 | 0x90100000 | 1 MB (实用 750 KB) | 帧缓冲 1 (RGB565) |
| MCU_IN | 0x90200000 | 1 MB (实用 576 KB) | JPEG 输入 (YCbCr 4:2:0) |
| JPEG_OUT | 0x90300000 | 256 KB | JPEG 输出 |
| 剩余 | 0x90340000+ | ~29 MB | 可用 |

## Phase 2 性能实测（未优化）

| 操作 | 耗时 | 说明 |
|------|------|------|
| fb_clear (DMA2D) | ~0.5 ms | 硬件填充 800×480 RGB565 |
| fill_rect (DMA2D) | <0.5 ms | 硬件填充矩形 |
| CPU 渲染 gradient | 39 ms | 逐像素 CPU 计算 |
| RGB565 → YCbCr convert | **159 ms** | ⚠️ 瓶颈, MCU 缓冲在 PSRAM |
| 硬件 JPEG encode | **33 ms** | ⚠️ 读 PSRAM 慢 |
| **组合 pipeline** | **230 ms/帧 = 4 fps** | 离 60fps 目标远 |
| JPEG 输出大小 | 13.7 KB/帧 | 56:1 压缩比（gradient @Q80） |

## 构建说明

- **SCons**: 顶层 `SConscript` 会自动扫描 `user/SConscript`，直接 `scons -j16` 即可
- **RT-Thread Studio (Eclipse CDT)**: 新建 user 目录后需要右键项目刷新(F5)，然后 Clean + Build
- **Clean Project 后** Eclipse 会根据 `.cproject` 的 `sourceEntries` 重新生成 subdir.mk。
  HAL_DMA2D / HAL_JPEG 必须从排除列表里移除才会编译（本工程已处理）

## 关键配置

| 项目 | 当前值 | 说明 |
|------|--------|------|
| `RT_MAIN_THREAD_STACK_SIZE` | **4096** | HAL_JPEG_Init 内部栈使用大（~810B 局部变量），2048 会溢出 |
| `HAL_DMA2D_MODULE_ENABLED` | 已定义 | 在 `stm32h7rsxx_hal_conf.h` 里打开 |
| `HAL_JPEG_MODULE_ENABLED` | 已定义 | 同上 |
| JPEG 质量 | Q80 | 质量/体积平衡，可在 `user_display_jpeg.h` 调 |
| JPEG 色度子采样 | 4:2:0 | 最高压缩率 |

## 注意事项

1. 帧缓冲位于 PSRAM，CPU 访问比 SRAM 慢。**小粒度写入（逐字节）性能极差**，优先用 DMA2D 或 32bit 连续写入
2. 使能了 D-Cache。CPU 写完帧缓冲后，若由 DMA/外设读取，必须 `SCB_CleanDCache_by_Addr`
3. MCU 输入缓冲（576KB）当前也在 PSRAM，**这是当前 JPEG pipeline 慢的主因**，Phase 2.5 考虑搬到 SRAM 分块
4. PSRAM 的硬件初始化由 Bootloader（XSPI1 memory-mapped 模式）完成

## 常见故障

### 启动后 `whd_init` 线程 HardFault / 断言失败

**症状**: 日志里 JPEG init 成功，但接着出现 `_thread_sleep` 断言失败，`whd_init` 崩溃。

**根因**: main 线程栈溢出，踩踏了相邻的 WHD TCB。`HAL_JPEG_Init` 内部 `JPEG_Set_HuffAC_Mem`
会在栈上分配 `JPEG_AC_HuffCodeTableTypeDef`（~810 字节）两次，main 默认 2048 字节栈放不下。

**修复**: `rtconfig.h` 里 `RT_MAIN_THREAD_STACK_SIZE` 提到 4096。（本工程已处理）

### 链接错误 `undefined reference to HAL_DMA2D_Init / HAL_JPEG_Init`

**根因**: `.cproject` 的 `sourceEntries` 里把 HAL 的 `dma2d.c` / `jpeg.c` 加入了排除列表。

**修复**: 本工程已移除，现在 Clean Project 后 Eclipse 会重新生成 subdir.mk 并包含它们。

### 下载烧录时 `DEV_TARGET_NOT_HALTED`

板子没有停下来（正在 XIP 跑旧固件）。按一下 RESET 键立刻点下载，或者 STM32CubeProgrammer
连接模式选 "Under Reset"。

## Phase 2.5 优化路线（TODO）

为把 pipeline 降到 <16 ms/帧（≥60 fps），可按如下顺序推进：

1. **convert 瓶颈**（当前 159ms）
   - MCU 输出缓冲从 PSRAM 挪到 SRAM（需要分块流水，96KB ~= 5 行 MCU 条带 × 2 buffer）
   - LUT 展开: 把 Y/Cb/Cr 合并到一张 64KB 查表避免 3 次乘法
   - 循环展开 / ITCM 执行

2. **encode 瓶颈**（当前 33ms）
   - 用 `HAL_JPEG_Encode_DMA` 替代 polling 版，让 JPEG 自己通过 DMA 读 MCU 缓冲
   - 若 MCU 仍在 PSRAM，AHB5 总线通道配置注意 burst length

3. **Pipeline 并行**
   - convert (CPU) + encode (DMA) 流水线
   - USB Bulk 传输（DMA）也可与下一帧编码重叠

预计优化后：convert ~20ms, encode ~5ms, total ~25ms, 约 40 fps。要稳定 60fps 还需要
convert 改用 DMA2D 或 硬件 ycbcr 转换器辅助。
