/*
 * wifi_provision.h - Wi-Fi 配网（热点 + 网页配置，支持保存多组网络）
 *
 * 可保存多组 Wi-Fi 凭据到 NVS；开机/断线时扫描周围网络，
 * 自动连接其中信号最好的一组已知网络。
 */
#pragma once
#include <Arduino.h>

/* 配网/连接过程中用于刷新屏幕的回调（三行文本） */
typedef void (*ProvisionDisplayFn)(const char* line1, const char* line2, const char* line3);

void wifiProvisionSetDisplay(ProvisionDisplayFn fn);

#define WIFI_MAX_NETS 8                  /* 最多保存的网络组数 */

/* NVS 中的多组 Wi-Fi 凭据 */
int  wifiProvisionCount();
int  wifiProvisionLoadList(String ssids[], String passes[], int maxN);
bool wifiProvisionAdd(const String& ssid, const String& pass);   /* 添加/更新一组 */
void wifiProvisionClear();                                       /* 清除全部 */

/* 阻塞式连接指定网络，返回是否成功 */
bool wifiProvisionConnect(const String& ssid, const String& pass, uint32_t timeoutMs);

/* 扫描周围网络，自动连接已知网络中信号最好的一组 */
bool wifiProvisionConnectAny(uint32_t perNetTimeoutMs, String& outSsid, String& outPass);

/* 开启配置热点并等待用户提交，成功则通过 outSsid/outPass 返回刚保存的网络 */
bool wifiProvisionRunPortal(uint32_t timeoutMs, String& outSsid, String& outPass);
