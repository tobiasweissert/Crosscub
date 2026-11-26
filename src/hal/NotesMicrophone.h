#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#ifndef SIMULATOR
#include <driver/i2s_pdm.h>
#endif
// One capture worker owns I2S calls. stop() only revokes and lowers the rail;
// it is safe from the input/lifecycle task even during a driver call.
class NotesMicrophone {
 public:
  bool begin();
  size_t read(uint8_t* out, size_t maximum);
  void stop();
  bool close();
 private:
  std::mutex gate;
  std::atomic<bool> revoked{false};
#ifndef SIMULATOR
  i2s_chan_handle_t channel = nullptr;
#endif
  bool enabled = false, closed = false, clean = true;
};
