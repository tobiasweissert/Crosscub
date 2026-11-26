#include "NotesMicrophone.h"
#include <Arduino.h>
#ifndef SIMULATOR
#include <driver/gpio.h>
#endif

void NotesMicrophone::stop() {
  std::lock_guard<std::mutex> lock(gate);
  revoked.store(true);
#ifndef SIMULATOR
  gpio_set_level(GPIO_NUM_38, 0);
#endif
}
bool NotesMicrophone::begin() {
#ifdef SIMULATOR
  return false;
#else
  {
    std::lock_guard<std::mutex> lock(gate);
    if (revoked.load() || closed) return false;
    gpio_set_level(GPIO_NUM_38, 0);
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << GPIO_NUM_38;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK || gpio_set_level(GPIO_NUM_38, 1) != ESP_OK) {
      gpio_set_level(GPIO_NUM_38, 0); return false;
    }
  }
  delay(10);  // onboard PDM microphone rail settling, as in the SDK driver
  if (revoked.load()) return false;
  i2s_chan_config_t config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  if (i2s_new_channel(&config, nullptr, &channel) != ESP_OK || revoked.load()) { stop(); return false; }
  i2s_pdm_rx_config_t pdm{};
  pdm.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(24000);
  pdm.slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  pdm.gpio_cfg.clk = GPIO_NUM_19;
  pdm.gpio_cfg.din = GPIO_NUM_20;
  if (i2s_channel_init_pdm_rx_mode(channel, &pdm) != ESP_OK || revoked.load()) { stop(); return false; }
  if (i2s_channel_enable(channel) != ESP_OK) { stop(); return false; }
  enabled = true;
  if (revoked.load()) { stop(); return false; }
  return true;
#endif
}
size_t NotesMicrophone::read(uint8_t* out, size_t maximum) {
#ifdef SIMULATOR
  (void)out; (void)maximum;
  return 0;
#else
  if (revoked.load() || !enabled || !maximum || (maximum & 1)) return 0;
  size_t count = 0;
  // Zero-tick IDF read: don't let per-DMA wait time extend the 30-second rail budget.
  const auto result = i2s_channel_read(channel, out, maximum, &count, 0);
  if (revoked.load()) return 0;
  if ((result != ESP_OK && result != ESP_ERR_TIMEOUT) || count > maximum || (count & 1)) { stop(); return 0; }
  return count;
#endif
}
bool NotesMicrophone::close() {
  if (closed) return clean;
  stop();
  closed = true;
#ifndef SIMULATOR
  if (channel) {
    if (enabled && i2s_channel_disable(channel) != ESP_OK) clean = false;
    if (clean && i2s_del_channel(channel) != ESP_OK) clean = false;
    if (clean) channel = nullptr;
  }
#endif
  return clean;
}
