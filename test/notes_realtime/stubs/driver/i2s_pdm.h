#pragma once
#include <cstddef>
#include <cstring>
#include <functional>
class NotesMicrophone;
inline NotesMicrophone* activeMic=nullptr;
inline std::function<void(NotesMicrophone*)> duringCreate;
inline unsigned creates=0,deletes=0,disables=0,sampleRate=0;inline int clk=0,data=0;
inline bool initError=false;
constexpr int ESP_OK=0,ESP_ERR_TIMEOUT=1,I2S_DATA_BIT_WIDTH_16BIT=16,I2S_SLOT_MODE_MONO=1,GPIO_NUM_19=19,GPIO_NUM_20=20;
enum i2s_port_t{I2S_NUM_AUTO=-1};
enum i2s_role_t{I2S_ROLE_MASTER=1};
using i2s_chan_handle_t=void*;
struct i2s_chan_config_t{i2s_port_t id;i2s_role_t role;unsigned dma_desc_num,dma_frame_num;bool auto_clear_after_cb,auto_clear_before_cb,allow_pd;int intr_priority;};
struct ClockCfg{unsigned sample_rate_hz=0;};struct SlotCfg{int data_bit_width=0,slot_mode=0;};
struct i2s_pdm_rx_config_t{ClockCfg clk_cfg;SlotCfg slot_cfg;struct {int clk=0,din=0;}gpio_cfg;};
// Keep the public IDF macro's heterogeneous brace-initializer shape.
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) {a,b,6,240,false,false,false,0}
#define I2S_PDM_RX_CLK_DEFAULT_CONFIG(rate) ClockCfg{rate}
#define I2S_PDM_RX_SLOT_DEFAULT_CONFIG(bits,mode) SlotCfg{bits,mode}
inline int i2s_new_channel(const i2s_chan_config_t*,void*,i2s_chan_handle_t* out){++creates;*out=reinterpret_cast<void*>(1);if(duringCreate)duringCreate(activeMic);return 0;}
inline int i2s_channel_init_pdm_rx_mode(i2s_chan_handle_t,const i2s_pdm_rx_config_t* c){sampleRate=c->clk_cfg.sample_rate_hz;clk=c->gpio_cfg.clk;data=c->gpio_cfg.din;return initError?-1:0;}
inline int i2s_channel_enable(i2s_chan_handle_t){return 0;}
inline int i2s_channel_disable(i2s_chan_handle_t){++disables;return 0;}
inline int i2s_del_channel(i2s_chan_handle_t){++deletes;return 0;}
inline int i2s_channel_read(i2s_chan_handle_t,void* p,size_t n,size_t* count,unsigned timeout){if(timeout!=0)return -1;memset(p,0,n);*count=n;return 0;}
