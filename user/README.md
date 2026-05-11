# USB 显示桥 (USB HS Display Bridge)

把板子的帧缓冲通过 USB HS Bulk + 硬件 JPEG 编码传到 PC 端窗口显示。
目标 60fps @ 800x480 RGB565。

## 架构

```
 用户渲染/LVGL/测试图案
         ↓
    帧缓冲 (PSRAM, 双缓冲, 768KB×2)
         ↓
    硬件 JPEG 编码器 (H7RS 内置)
         ↓
    USB OTG HS Bulk IN 端点
         ↓
    PC 接收程序 (Python + SDL2)
         ↓
    PC 窗口
```

## 目录结构

```
user/
├── inc/
│   ├── user_display.h           主控 API
│   ├── user_display_fb.h        帧缓冲
│   ├── user_test_pattern.h      测试图案
│   ├── user_display_jpeg.h      JPEG 编码 (Phase 2)
│   └── user_display_usb.h       USB Bulk (Phase 3)
├── src/
│   ├── user_display.c
│   ├── user_display_fb.c
│   ├── user_test_pattern.c
│   ├── user_display_jpeg.c      stub
│   ├── user_display_usb.c       stub
│   └── user_shell.c             msh 命令
├── pc_viewer/                   PC 端查看器 (Phase 3)
├── Kconfig
├── SConscript
└── README.md
```

## 实现阶段

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | 帧缓冲 + 测试图案 + 基础框架 | ✅ 当前 |
| Phase 2 | 硬件 JPEG 编码器集成 | ⏳ 待做 |
| Phase 3 | USB HS Bulk + PC 查看器 | ⏳ 待做 |

## Phase 1 使用

在 msh 控制台：

```
udisp_init       # 初始化帧缓冲, 测试 PSRAM
udisp_test       # 在后台缓冲渲染一帧测试图案
udisp_stat       # 打印帧缓冲状态 / 渲染耗时
```

## 内存布局 (PSRAM @ 0x90000000, 32MB)

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| FB0 | 0x90000000 | 1 MB (实用 768 KB) | 帧缓冲 0 |
| FB1 | 0x90100000 | 1 MB (实用 768 KB) | 帧缓冲 1 |
| JPEG_OUT | 0x90200000 | 256 KB | JPEG 输出缓冲 |
| 剩余 | 0x90240000+ | ~29 MB | 可用于堆/其他 |

## 构建说明

- **SCons**: 顶层 `SConscript` 会自动扫描 `user/SConscript`，直接 `scons -j16` 即可
- **RT-Thread Studio (Eclipse CDT)**: 新建 user 目录后需要右键项目刷新(F5)，然后 Clean + Build，
  CDT 才会把新文件纳入构建

## 注意事项

1. 帧缓冲位于 PSRAM，访问速度比 SRAM 慢约 2~3 倍
2. 使能了 D-Cache，CPU 写入帧缓冲后，若由外设/DMA 读取需要 `SCB_CleanDCache_by_Addr`
3. PSRAM 的硬件初始化由 Bootloader 完成 (XSPI1 Hexa-SPI 16线)，应用无需重新配置

## 故障排查

### 问题: `udisp_init` 打印 "PSRAM test fail" 或直接 HardFault

说明 PSRAM 尚未处于 memory-mapped 模式。原因可能是：

- Bootloader 没有配置 XSPI1 memory-mapped
- 当前使用的是只跑 Appli 的场景，没有经过 Boot

**临时验证**: 用 msh 命令 `psram_test` (需要 menuconfig 打开 `BSP_USING_PSRAM`) 能否通过。

**长期方案**: 如果不想依赖 Boot 配好 PSRAM，可以在 `user_display_fb_init()` 前做 XSPI1 memory-map 配置。
这一步属于板级初始化，放进 `board/drv_psram.c` 并在 menuconfig 里启用 `BSP_USING_PSRAM` 是更干净的做法。

### 问题: 渲染一帧耗时几十毫秒

正常，PSRAM 的 CPU 写入带宽受 XSPI 接口限制。Phase 2 会改用 DMA2D 填充，速度会快一个量级。
