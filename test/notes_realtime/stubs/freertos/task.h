#pragma once
#include <thread>
#include <atomic>
#include "FreeRTOS.h"
inline std::atomic<bool> holdTasks{false};
inline int xTaskCreate(void(*fn)(void*),const char*,unsigned,void* arg,unsigned,void*){std::thread([fn,arg]{while(holdTasks.load())std::this_thread::yield();fn(arg);}).detach();return pdPASS;}
inline void vTaskDelete(void*){}
