# ESP32-C3 BPC 68.5 kHz 授时站

基于 **ESP32-C3 + 0.42" OLED** 的 **BPC（中国国家标准低频时码，68.5 kHz）授时信号发射站**。

设备通过 Wi-Fi 从 NTP 获取精确时间，在 **GPIO0** 上产生 68.5 kHz 载波并经磁耦合线圈天线发射符合 BPC 格式的时间码；OLED 用点阵字体显示 **时:分:秒**，板载 LED 指示授时状态。

> ⚠️ **免责声明**：本项目仅供实验、学习与近场测试使用。请使用小尺寸线圈天线、保持极低发射功率，并遵守当地无线电管理法规，**不得**连接大尺寸天线或进行远距离发射。

---

## 功能特性

| 功能 | 说明 |
| --- | --- |
| 授时信号 | BPC 68.5 kHz，20 秒一帧，四进制符号（100/200/300/400 ms 载波关断） |
| 天线输出 | GPIO0，经串联电阻驱动磁耦合线圈 |
| 时间同步 | Wi-Fi STA + NTP（阿里 / 腾讯 / cn.pool），中国标准时间 UTC+8 |
| 首次配网 | 无凭据时开启开放热点 `BPC-TimeStation-XXXX`，手机/电脑网页配置 Wi-Fi |
| 显示 | 0.42" OLED，SSD1306 128×64 显存、可见 72×40、偏移 (30,12)，点阵字体显示时:分:秒 |
| 状态指示 | 板载 LED（GPIO8，低电平点亮） |
| 强制对时 | 长按 BOOT 键（GPIO9）1 秒 |
| 云端编译 | GitHub Actions 自动编译并发布，本地无需安装工具链 |

---

## 硬件

| 项目 | 引脚 | 备注 |
| --- | --- | --- |
| BPC 天线输出 | **GPIO0** | 串 330 Ω（≥330Ω）后接线圈，线圈另一端接 GND |
| 板载 LED | GPIO8 | 低电平点亮 |
| BOOT 按键 | GPIO9 | 长按 1s 重新对时；长按 3s 清除 Wi-Fi 并重开配网热点 |
| OLED SDA | GPIO5 | I2C，地址 0x3C |
| OLED SCL | GPIO6 | I2C，400 kHz |

### 天线接法

```
GPIO0 ──[ 330 Ω ]──( 线圈 ~20 圈 )── GND
```

使用 0.3 mm 漆包线绕约 20 圈，直径与到电波表的距离相当即可；将电波表放在约 20 cm 以内对时成功率最高。

---

## 编译（不在本地编译）

固件在 **GitHub Actions** 上编译。把本仓库推到 GitHub 后，会触发 `.github/workflows/build.yml`：

1. 安装 PlatformIO 与 esptool；
2. 用仓库 Secrets 生成 `include/secrets.h`；
3. `pio run -e esp32-c3` 编译；
4. 合并 bootloader + 分区表 + 应用为一个可从 `0x0` 刷写的镜像；
5. 上传构建产物，并维护一个滚动的 **`latest` Release**（含固定名 `bpc-time-station.bin`）。

### Wi-Fi 配网（两种方式）

**方式一：设备热点配网（推荐，改 Wi-Fi 无需重新编译）**

首次上电（或长按 BOOT 3 秒清除后）设备没有 Wi-Fi 凭据时，会开启一个**开放热点**：

1. 手机 / 电脑连接热点 `BPC-TimeStation-XXXX`（XXXX 为芯片 ID）；
2. 浏览器打开 `http://192.168.4.1`（多数手机会自动弹出配置页）；
3. 选择 / 输入 2.4GHz Wi-Fi 名称与密码，点击“保存并连接”。

凭据保存在设备 NVS（`bpcwifi` 命名空间）中，**断电、重启、以及默认的增量刷写都不会丢失**，之后每次上电自动连接。运行中如需更换网络，长按 BOOT 键 3 秒，设备会清除凭据并重启进入配网热点（仅 `-Full` 整片刷写或 `erase_flash` 会清空）。

**方式二：编译时内置凭据（可选）**

在 **Settings → Secrets and variables → Actions** 添加：

| Secret | 说明 |
| --- | --- |
| `WIFI_SSID` | Wi-Fi 名称 |
| `WIFI_PASS` | Wi-Fi 密码 |

固件在没有 NVS 凭据时会使用它们直接连接；两者都没有则进入热点配网。

---

## 下载 + 重命名 + 刷写（一键）

先在 GitHub 上把仓库变成"有 `latest` Release"（推一次代码即可）。

Windows PowerShell **5.1 或 7 均可**（脚本为纯 ASCII，兼容两种版本）：

```powershell
# PowerShell 7
pwsh -File tools\flash.ps1
# 或 Windows PowerShell 5.1
powershell -ExecutionPolicy Bypass -File tools\flash.ps1
```

脚本会自动：

1. 从 `latest` Release 下载分区块固件（走 `api.github.com`，即使 `github.com` 直连受限也可用）；
2. 按版本号保存到 `firmware/`；
3. 自动安装 esptool（如缺失）；
4. 自动探测串口，按分区刷写并复位。

### 默认：增量刷写，保留配网信息

默认写入 4 个分区：

```
0x0     bpc-bootloader.bin
0x8000  bpc-partitions.bin
0xe000  bpc-boot_app0.bin
0x10000 bpc-app.bin
```

NVS 分区在 `0x9000`，**不在这四个区间内**，所以**升级固件不会丢失已保存的 Wi-Fi 配网信息**。

### 可选：整片刷写（首次 / 救砖）

用单个合并镜像整片写入 `0x0`，方便但会**清空 NVS（配网信息丢失）**：

```powershell
pwsh -File tools\flash.ps1 -Full
```

若刷写时找不到设备，请按住 **BOOT** 键再插 USB 进入下载模式，然后指定 `-Port` 重试。

常用参数：

```powershell
-Repo owner/name   # 覆盖自动识别的仓库
-Port COM5         # 指定串口
-Token ghp_xxx     # 私有仓库需要 Token
-NoFlash           # 只下载不刷写
-Full              # 合并镜像整片刷写（清空配网）
```

> 本地只需 Python + esptool 用于**刷写**，不需要任何编译工具链。

---

## LED 状态含义

| LED | 含义 |
| --- | --- |
| 慢闪（约 0.35 s 周期） | 未连接 Wi-Fi |
| 中速闪（约 0.3 s 周期） | 配网热点已开启，OLED 显示 `AP:...`，等待手机/电脑配置 |
| 快闪（约 0.12 s 周期） | 已连 Wi-Fi，正在等待 NTP 对时 |
| 每秒一次短脉冲 | **已授时**，正在发射 BPC 时间码 |

---

## OLED 显示

```
  BPC 68.5kHz      <- 载波频率
   12:34:56        <- 点阵字体，时:分:秒
    SYNC OK        <- WiFi ... / NTP ... / SYNC OK
```

---

## 项目结构

```
platformio.ini                     PlatformIO 工程（GitHub Actions 使用）
include/config.h                   硬件与运行参数
include/bpc.h, src/bpc.cpp         BPC 时间码编码
include/wifi_provision.h, src/wifi_provision.cpp   热点 / 网页配网
src/main.cpp                       主程序（Wi-Fi/NTP/OLED/LED/信号发生）
include/secrets.example.h          Wi-Fi 凭据示例（复制为 secrets.h）
.github/workflows/build.yml        云端编译 + 发布 Release
tools/flash.ps1                    一键下载 + 重命名 + 刷写
```

---

## 工作原理

- **载波**：LEDC 在 GPIO0 产生 68.5 kHz、50% 占空比方波。
- **时间码**：每 20 秒一帧。每整秒起始处把载波关断，关断时长表示一个四进制符号：

  | 符号 | 关断时长 | 比特 |
  | --- | --- | --- |
  | 0 | 100 ms | 00 |
  | 1 | 200 ms | 01 |
  | 2 | 300 ms | 10 |
  | 3 | 400 ms | 11 |

  帧内依次编码：帧序号、时、分、星期、日、月、年以及 P1/P2 偶校验（详见 `src/bpc.cpp`）。
- **对时**：`configTime()` 通过 NTP 写入系统时间，取 `gettimeofday()` 的整秒对齐发射。

---

## 参考资料

- [BPC (time signal) — Wikipedia](https://en.wikipedia.org/wiki/BPC_(time_signal))
- [bpcTransmitterEsp32](https://github.com/)（BPC 编码参考项目）
- [dcfake77](https://github.com/luigicalligaris/dcfake77)（DCF77 发射实现，LEDC 载波思路参考）

## 许可

MIT License，详见 [LICENSE](LICENSE)。
