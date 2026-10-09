/*
 * secrets.example.h
 * 复制为 include/secrets.h 并填入你的 Wi-Fi 凭据（本地编译时使用）。
 * 在 GitHub Actions 上，该文件会自动根据仓库 Secrets(WIFI_SSID / WIFI_PASS) 生成。
 * include/secrets.h 已被 .gitignore 忽略，不会提交到仓库。
 */
#pragma once

#define WIFI_SSID      "your-ssid"
#define WIFI_PASSWORD  "your-password"
