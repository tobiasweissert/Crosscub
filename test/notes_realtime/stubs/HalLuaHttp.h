#pragma once
inline bool wifiUp = true;
namespace HalLuaHttp {
inline bool wifiConnected() { return wifiUp; }
}  // namespace HalLuaHttp
