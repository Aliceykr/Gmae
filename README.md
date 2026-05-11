# ART-Pi2 WiFi 工程

## 硬件平台

| 项目 | 规格 |
|------|------|
| 板卡 | ART-Pi2 |
| MCU | STM32H7R7L8H6H (Cortex-M7, 600MHz max, 当前配置 480MHz) |
| 内部 SRAM | 456 KB AXI SRAM (0x24000000) |
| 外部 PSRAM | 32 MB HSPI PSRAM (0x90000000, XSPI1 Hexa-SPI 16线) |
| 外部 NOR Flash | 64 MB Winbond (0x70000000, XSPI2 Octo-SPI 8线, XIP执行) |
| WiFi | CYW43438 (2.4GHz 802.11b/g/n) |
| SD 卡 | SDIO1, 热插拔检测 (PN7) |
| 调试串口 | UART4 (PD0/PD1) |
| LED | PO5 (蓝色) |

## 软件环境

- **RTOS**: RT-Thread 5.1.0
- **网络栈**: lwIP 2.1.2
- **WiFi 驱动**: wifi-host-driver (WHD 3.1.0, Cypress/Infineon)
- **文件系统**: ELM FatFs (SD卡) + ROMFS
- **构建工具**: RT-Thread Studio / SCons + GCC ARM

## Flash 分区表 (NOR Flash 64MB)

| 分区名 | 偏移 | 大小 | 用途 |
|--------|------|------|------|
| wifi_image | 0 | 512 KB | WiFi 固件 |
| bt_image | 512 KB | 512 KB | 蓝牙固件 (预留) |
| download | 1 MB | 2 MB | OTA 下载区 |
| easyflash | 3 MB | 1 MB | KV 存储 |
| filesystem | 4 MB | 12 MB | 文件系统 |

> 注: 代码段从 Flash 起始地址 XIP 执行, 链接脚本分配了前 8MB 给代码区。

## 快速开始

### 编译

```bash
# SCons 方式
pkgs --update          # 拉取 wifi-host-driver 和 netutils 软件包
scons -j16             # 编译

# 或生成 IDE 工程
scons --target=mdk5    # Keil MDK
scons --target=iar     # IAR
```

RT-Thread Studio 用户直接在 IDE 中编译即可。

### 下载

将开发板 ST-Link USB 口连接 PC, 通过 ST-Link 下载固件。

### WiFi 固件准备

WiFi 驱动固件需要预先烧录到 NOR Flash 的 `wifi_image` 分区, 或者将以下文件放到 SD 卡根目录:

```
packages/wifi-host-driver-latest/wifi-host-driver/WiFi_Host_Driver/resources/firmware/COMPONENT_43438/43438A1.bin
packages/wifi-host-driver-latest/wifi-host-driver/WiFi_Host_Driver/resources/clm/COMPONENT_43438/43438A1.clm_blob
```

### 运行

```
 \ | /
- RT -     Thread Operating System
 / | \     5.1.0 build Oct 30 2024 15:31:59
 2006 - 2024 Copyright by RT-Thread team
lwIP-2.0.3 initialized!
[I/sal.skt] Socket Abstraction Layer initialize success.
msh />[I/SDIO] SD card capacity 31166976 KB.
[I/app.filesystem] sd card mount to '/sdcard'
WLAN MAC Address : 2C:B0:FD:A2:26:76
WLAN Firmware    : wl0: Mar 28 2021 22:55:55 version 7.45.98.117
[I/WLAN.dev] wlan init success
```

### 常用命令

```bash
# 连接 WiFi
wifi join <SSID> <密码>

# 查看 IP
ifconfig

# 网络测速
iperf -c <服务端IP>

# 查看文件系统
ls /sdcard
```

## 目录结构

```
├── applications/       用户应用 (main.c)
├── board/              板级支持
│   ├── port/           FAL Flash 端口、文件系统挂载
│   ├── CubeMX_Config/  STM32CubeMX 生成代码
│   └── linker_scripts/ 链接脚本
├── libraries/          BSP 驱动和 HAL 库
│   ├── drivers/        GPIO/UART/SDMMC/SPI/PSRAM/WLAN 等驱动
│   ├── STM32H7RSxx_HAL_Driver/
│   └── CMSIS/
├── packages/           在线软件包
│   ├── wifi-host-driver-latest/
│   └── netutils-latest/
├── rt-thread/          RT-Thread 内核源码
├── rtconfig.h          系统配置 (Kconfig 生成)
└── rtconfig_preinc.h   预包含头文件
```

## 注意事项

1. 首次编译前务必执行 `pkgs --update` 拉取软件包
2. 编译优化等级可调至 `-O2` 或 `-O3` 以提升 iperf 测速性能
3. WiFi 国家码当前设置为 AU, 如需修改请在 menuconfig 中调整 `WHD_COUNTRY_CODE`
4. 本工程可作为 IoT 应用的基础模板, 在 `main.c` 中添加业务逻辑即可
