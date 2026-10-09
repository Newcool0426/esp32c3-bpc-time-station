/*
 * wifi_provision.h - Wi-Fi 配网（热点 + 网页配置）
 *
 * 无已保存凭据时开启开放热点，用户用手机/电脑连接后在网页里
 * 填写 SSID/密码，凭据保存到 NVS，设备随后自动连接。
 */
#pragma once
#include <Arduino.h>

/* 配网/连接过程中用于刷新屏幕的回调（三行文本） */
typedef void (*ProvisionDisplayFn)(const char* line1, const char* line2, const char* line3);

void wifiProvisionSetDisplay(ProvisionDisplayFn fn);

/* NVS 中的 Wi-Fi 凭据 */
bool wifiProvisionLoad(String& ssid, String& pass);
void wifiProvisionSave(const String& ssid, const String& pass);
void wifiProvisionClear();

/* 阻塞式连接，返回是否成功 */
bool wifiProvisionConnect(const String& ssid, const String& pass, uint32_t timeoutMs);

/* 开启配置热点并等待用户提交，成功则通过 outSsid/outPass 返回 */
bool wifiProvisionRunPortal(uint32_t timeoutMs, String& outSsid, String& outPass);
