#pragma once
#include <cstdint>
#include <atomic>
inline std::atomic<bool> rail{false};
using gpio_num_t=int;
constexpr int GPIO_NUM_38=38,GPIO_MODE_OUTPUT=1,GPIO_PULLUP_DISABLE=0,GPIO_PULLDOWN_DISABLE=0,GPIO_INTR_DISABLE=0;
struct gpio_config_t {uint64_t pin_bit_mask=0;int mode=0,pull_up_en=0,pull_down_en=0,intr_type=0;};
inline int gpio_set_level(int pin,int level){if(pin==38)rail=level;return 0;}
inline int gpio_config(const gpio_config_t* c){return c->pin_bit_mask==(1ULL<<38)?0:-1;}
