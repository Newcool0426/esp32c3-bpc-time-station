/*
 * config.h - 硬件与运行参数配置
 *
 *  WiFi 凭据有两种来源：
 *    1) 本地/CI 生成的 include/secrets.h（见 secrets.example.h，已被 .gitignore 忽略）
 *    2) GitHub Actions 用仓库 Secrets(WIFI_SSID / WIFI_PASS) 生成 secrets.h
 *  若两者都没有，则使用下面的占位符（设备会一直闪灯提示未连接）。
 */
#pragma once
#include <stdint.h>

#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif

#ifndef WIFI_SSID
#  define WIFI_SSID      "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#  define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
#endif

/* ---------- 授时天线 / BPC 载波输出 ---------- */
#define BPC_PIN             0            /* 磁耦合线圈天线 -> GPIO0 */
#define BPC_FREQ_HZ         68500        /* BPC 载波频率 68.5 kHz */
#define BPC_LEDC_CHANNEL    0

/* 负脉冲期间的载波占空比(0-255)：
 *   0  = 完全关断 (100% 调幅，接收端 AGC 易失压)
 *   20 ≈ 8% 占空比，基波幅度约 25%，接近 BPC 规范的 10~30% 负脉冲
 * 若你的接收端仍不对时，可在此微调（例如 12 / 28 / 36）。 */
#define BPC_PULSE_DUTY      20

/* ---------- 板载蓝色 LED（GPIO8，低电平点亮） ---------- */
#define LED_PIN             8
#define LED_ACTIVE_LOW      1

/* ---------- BOOT 按键（GPIO9），长按 1s 强制重新对时 ---------- */
#define BOOT_BTN_PIN        9

/* ---------- 0.42" OLED ----------
 * 可见区域 72x40；驱动芯片为 SSD1306，显存 128x64；
 * 可见区域在显存中的偏移为 (30, 12)。
 */
#define OLED_SDA            5
#define OLED_SCL            6
#define OLED_ADDR           0x3C
#define OLED_OFF_X          30
#define OLED_OFF_Y          24
#define OLED_W              72
#define OLED_H              40
#define OLED_RENDER_DELAY_MS 450   /* 屏幕在“整秒后 450ms”刷新，使秒数跳动均匀 */

/* ---------- NTP ---------- */
#define NTP_SERVER1         "ntp.aliyun.com"
#define NTP_SERVER2         "ntp.tencent.com"
#define NTP_SERVER3         "cn.pool.ntp.org"
#define TZ_OFFSET_SEC       (8 * 3600)        /* 中国标准时间 UTC+8 */
#define DST_OFFSET_SEC      0
#define NTP_POLL_MS         (1UL * 3600UL * 1000UL)   /* 自动 NTP 轮询：每 1 小时 */
#define NTP_FORCE_MS        (2UL * 3600UL * 1000UL)   /* 强制重新对时：每 2 小时 */

#define FW_NAME             "esp32c3-bpc-time-station"

/* ---------- Wi-Fi 配网（热点 + 网页配置） ----------
 * 首次上电若无已保存凭据，设备会开启一个开放热点，
 * 手机/电脑连上后浏览器会自动弹出（或访问 192.168.4.1）
 * 配置页面，填写要连接的 Wi-Fi 后即可自动连接。
 */
#define AP_SSID_PREFIX            "BPC-TimeStation"
#define WIFI_CONNECT_TIMEOUT_MS   15000
#define PORTAL_TIMEOUT_MS         (5UL * 60UL * 1000UL)
#define WIFI_RESET_HOLD_MS        3000    /* 运行中长按 BOOT 3s 清除配网并重开热点 */
