#pragma once
#include "AppConfig.h"
#ifndef MIMIVENT_WIFI_READ_CONTROL
#define MIMIVENT_WIFI_READ_CONTROL 0
#endif
#if MIMIVENT_ENABLE_WIFI && MIMIVENT_WIFI_READ_CONTROL
#include <cJSON.h>
struct WifiAuthorization { bool valid, pending; char tokenId[33]; char serviceId[33]; uint32_t authRevision; };
void wifiBindingBegin();
void wifiBindingUpdate();
bool wifiBindingReady();
const char* wifiBindingState();
// 硬件派生设备标识（32 hex）：擦除配置、升级、重启后保持稳定。
const char* wifiBindingDeviceId();
String wifiBindingRequest(cJSON* root,const char* owner);
// 校验服务凭据；pending 表示候选授权，调用方必须拒绝普通控制。
WifiAuthorization wifiBindingAuthorize(const char* token,uint32_t now);
bool wifiBindingOwnerAllowed(const char* owner);
bool wifiBindingRevokeAllowed(const char* owner,const char* tokenId);
// 把单服务授权身份附加到 MQTT 状态：service 字段供 Gateway 核验；不输出 token 摘要。
bool wifiBindingAddAuthTokens(cJSON* state);
#endif
