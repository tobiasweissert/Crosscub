#pragma once
#include <atomic>
#include <chrono>
#include <thread>
inline std::atomic<unsigned> testTick{0};
inline unsigned millis() { return testTick.load(); }
inline void delay(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
