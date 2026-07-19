// 复制本文件为 secrets.h 并填入真实值。secrets.h 不会提交到 Git。
#pragma once

// 智谱 AI API Key：https://open.bigmodel.cn/ → API Keys
// 格式多为「id.secret」，Bearer 鉴权
#ifndef OPENAI_API_KEY
#define OPENAI_API_KEY "YOUR_ZHIPU_API_KEY"
#endif

// 手机热点或家里 WiFi（ESP 需要上网才能解题）
#ifndef SECRETS_WIFI_SSID
#define SECRETS_WIFI_SSID ""
#endif
#ifndef SECRETS_WIFI_PASS
#define SECRETS_WIFI_PASS ""
#endif
