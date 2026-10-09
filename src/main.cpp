/*
 * ============================================================================
 *  ESP32-C3 BPC 68.5 kHz 授时站 / Time-signal station
 * ----------------------------------------------------------------------------
 *  - 载波：68.5 kHz (LEDC) 从 GPIO0 经串联电阻驱动磁耦合线圈天线
 *  - 时间：Wi-Fi 连接 + NTP 同步（中国标准时间 UTC+8）
 *  - 显示：0.42" OLED (SSD1306 128x64 显存, 可见 72x40, 偏移 30,12)
 *          点阵字体显示 时:分:秒
 *  - 指示：板载 LED (GPIO8, 低电平点亮) 指示授时状态
 *  - 信号：每 20 秒一帧的 BPC 四进制时间码
 *
 *  编译在 GitHub Actions 上完成；本地用 tools/flash.ps1 直接刷写。
 * ============================================================================
 */
#include <Arduino.h>
#if !defined(ESP_ARDUINO_VERSION_MAJOR)
#  include <esp_arduino_version.h>
#endif
#if !defined(ESP_ARDUINO_VERSION_MAJOR)
#  define ESP_ARDUINO_VERSION_MAJOR 2
#endif

#include <WiFi.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>
#include <sys/time.h>

#include "config.h"
#include "bpc.h"

/* ---------------------------------------------------------------- OLED --- */
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

/* --------------------------------------------------------------- 状态 ---- */
static uint8_t  bpcSym[20];          /* 当前帧 20 个四进制符号 */
static bool     timeValid    = false; /* NTP 时间是否已同步 */
static bool     wifiUp       = false;
static time_t   lastSec      = 0;

static bool     carrierOff   = false; /* 当前是否处于负脉冲(关断)期 */
static uint32_t offUntilUs   = 0;

static bool     ledPulseOn   = false; /* 每秒脉冲指示 */
static uint32_t ledPulseEnd  = 0;

static uint32_t lastWifiTry  = 0;
static uint32_t lastNtpMs    = 0;
static uint32_t lastHouseMs  = 0;
static int      lastStatus   = -1;

static bool     btnWasDown   = false;
static uint32_t btnDownMs    = 0;

/* ------------------------------------------------------------- 前向声明 -- */
static void maintainNet(uint32_t nowMs);
static void maintainButton(uint32_t nowMs);
static void signalTick();
static void updateLed(uint32_t nowMs);

/* ---------------------------------------------------------------- LED ---- */
static inline void ledWrite(bool on)
{
#if LED_ACTIVE_LOW
    digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

/* -------------------------------------------------------------- 载波 ----- */
static void carrierInit()
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttachChannel(BPC_PIN, BPC_FREQ_HZ, 8, BPC_LEDC_CHANNEL);
#else
    ledcSetup(BPC_LEDC_CHANNEL, BPC_FREQ_HZ, 8);
    ledcAttachPin(BPC_PIN, BPC_LEDC_CHANNEL);
#endif
}

static inline void carrierOn()
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteChannel(BPC_LEDC_CHANNEL, 127);   /* 50% 占空比 */
#else
    ledcWrite(BPC_LEDC_CHANNEL, 127);
#endif
}

static inline void carrierSilent()
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteChannel(BPC_LEDC_CHANNEL, 0);
#else
    ledcWrite(BPC_LEDC_CHANNEL, 0);
#endif
}

/* -------------------------------------------------------------- OLED ----- */
static void centerStr(int baselineY, const char* s)
{
    int w = (int)u8g2.getStrWidth(s);
    int x = OLED_OFF_X + (OLED_W - w) / 2;
    if (x < OLED_OFF_X) x = OLED_OFF_X;
    u8g2.drawStr(x, baselineY, s);
}

static const char* statusLine()
{
    if (!wifiUp)    return "WiFi ...";
    if (!timeValid) return "NTP ...";
    return "SYNC OK";
}

/* 通用三行状态页 */
static void drawStatusScreen(const char* top, const char* mid, const char* bottom)
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_4x6_tr);
    centerStr(OLED_OFF_Y + 6, top);
    u8g2.setFont(u8g2_font_7x13B_tr);
    centerStr(OLED_OFF_Y + 24, mid);
    u8g2.setFont(u8g2_font_4x6_tr);
    centerStr(OLED_OFF_Y + 38, bottom);
    u8g2.sendBuffer();
}

/* 点阵字体显示 时:分:秒 */
static void renderClock(const struct tm* t)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             t->tm_hour, t->tm_min, t->tm_sec);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_4x6_tr);
    centerStr(OLED_OFF_Y + 6, "BPC 68.5kHz");
    u8g2.setFont(u8g2_font_7x13B_tr);
    centerStr(OLED_OFF_Y + 24, buf);
    u8g2.setFont(u8g2_font_4x6_tr);
    centerStr(OLED_OFF_Y + 38, statusLine());
    u8g2.sendBuffer();
}

/* =============================================================== setup === */
void setup()
{
    /* LED */
    pinMode(LED_PIN, OUTPUT);
    ledWrite(false);

    /* BOOT 按键 */
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("=== ESP32-C3 BPC 68.5kHz 授时站 ===");

    /* OLED */
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000);
    u8g2.setI2CAddress(OLED_ADDR << 1);
    u8g2.begin();
    u8g2.setBusClock(400000);
    u8g2.setFontMode(1);
    u8g2.setFontPosBaseline();
    drawStatusScreen("ESP32-C3", "BPC TX", "68.5 kHz");

    /* 载波：先静默，等时间有效后再开启 */
    carrierInit();
    carrierSilent();
    Serial.printf("BPC carrier %d Hz on GPIO%d\n", BPC_FREQ_HZ, BPC_PIN);

    /* Wi-Fi */
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    Serial.printf("Connecting to WiFi SSID \"%s\" ...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t now = millis();
    lastWifiTry = lastNtpMs = lastHouseMs = now;
}

/* ================================================================ loop === */
void loop()
{
    uint32_t nowMs = millis();

    maintainNet(nowMs);
    maintainButton(nowMs);

    if (timeValid) {
        signalTick();
    }

    updateLed(nowMs);

    /* 状态页刷新（仅在状态变化时） */
    if (nowMs - lastHouseMs >= 300) {
        lastHouseMs = nowMs;
        int st = !wifiUp ? 0 : (timeValid ? 2 : 1);
        if (st != lastStatus) {
            lastStatus = st;
            if (!timeValid) {
                drawStatusScreen("ESP32-C3", "BPC TX", statusLine());
            }
        }
    }

    delay(1);
}

/* ------------------------------------------------------------ Wi-Fi/NTP -- */
static void maintainNet(uint32_t nowMs)
{
    wl_status_t st = WiFi.status();

    if (st == WL_CONNECTED) {
        if (!wifiUp) {
            wifiUp = true;
            Serial.print("WiFi connected, IP = ");
            Serial.println(WiFi.localIP());
            configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC,
                       NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
            lastNtpMs = nowMs;
        }

        if (!timeValid) {
            time_t now = time(nullptr);
            if (now > 1600000000) {           /* 时间已由 SNTP 写入 */
                timeValid = true;
                struct tm t;
                localtime_r(&now, &t);
                bpc_encode(&t, bpcSym);       /* 预生成当前帧 */
                Serial.printf("NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                              t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                              t.tm_hour, t.tm_min, t.tm_sec);
            }
        } else if ((uint32_t)(nowMs - lastNtpMs) > NTP_RESYNC_MS) {
            configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC,
                       NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
            lastNtpMs = nowMs;
            Serial.println("NTP periodic resync");
        }
    } else {
        wifiUp = false;
        /* 断线重连（避免过于频繁地打断正在进行的连接） */
        if ((st == WL_DISCONNECTED || st == WL_CONNECT_FAILED ||
             st == WL_NO_SSID_AVAIL) &&
            (uint32_t)(nowMs - lastWifiTry) > 10000) {
            lastWifiTry = nowMs;
            Serial.println("WiFi reconnect...");
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }
}

/* ------------------------------------------------------------ BOOT 按键 -- */
static void maintainButton(uint32_t nowMs)
{
    bool down = (digitalRead(BOOT_BTN_PIN) == LOW);
    if (down) {
        if (btnDownMs == 0) {
            btnDownMs = nowMs;
        } else if (!btnWasDown && (uint32_t)(nowMs - btnDownMs) > 1000) {
            btnWasDown = true;
            Serial.println("BOOT held -> force NTP resync");
            configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC,
                       NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
            lastNtpMs = nowMs;
        }
    } else {
        btnDownMs = 0;
        btnWasDown = false;
    }
}

/* ------------------------------------------------------- 授时信号发生 ---- */
static void signalTick()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint32_t nowUs = micros();

    if (tv.tv_sec != lastSec) {
        lastSec = tv.tv_sec;

        struct tm t;
        localtime_r(&tv.tv_sec, &t);
        int fs = t.tm_sec % 20;

        if (fs == 0) {
            bpc_encode(&t, bpcSym);         /* 每 20 秒重建一帧 */
        }

        int offMs = (bpcSym[fs] == BPC_NO_GAP) ? 0 : (int)bpcSym[fs] * 100;

        if (carrierOff) {                   /* 上一秒的负脉冲收尾 */
            carrierOn();
            carrierOff = false;
        }
        if (offMs > 0) {
            carrierSilent();                /* 整秒开始处的负脉冲 */
            carrierOff = true;
            offUntilUs = nowUs + (uint32_t)offMs * 1000;
        }

        /* 每秒一个 LED 脉冲，指示"正在授时" */
        ledPulseOn = true;
        ledPulseEnd = millis() + 40;
        ledWrite(true);

        renderClock(&t);
    }

    if (carrierOff && (int32_t)(nowUs - offUntilUs) >= 0) {
        carrierOn();
        carrierOff = false;
    }
}

/* ---------------------------------------------------------------- LED ---- */
static void updateLed(uint32_t nowMs)
{
    if (timeValid) {
        if (ledPulseOn && (int32_t)(nowMs - ledPulseEnd) >= 0) {
            ledPulseOn = false;
            ledWrite(false);
        }
        return;
    }

    /* 尚未授时：闪灯提示 */
    static uint32_t nextToggle = 0;
    static bool     state      = false;
    uint32_t period = wifiUp ? 120 : 350;   /* 有网无时间(对时中)更快 */
    if ((int32_t)(nowMs - nextToggle) >= 0) {
        state = !state;
        ledWrite(state);
        nextToggle = nowMs + period;
    }
}
