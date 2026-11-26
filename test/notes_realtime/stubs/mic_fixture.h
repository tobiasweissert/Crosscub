#pragma once
#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
inline void resetMic(){rail=false;creates=deletes=disables=sampleRate=0;clk=data=0;initError=false;duringCreate={};activeMic=nullptr;}
