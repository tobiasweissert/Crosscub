#include "hal/NotesMicrophone.h"
#include <cassert>
#include <cstdio>
#include "mic_fixture.h"
int main(){
 {NotesMicrophone mic;mic.stop();assert(!mic.begin());assert(!rail && creates==0);}
 resetMic();
 {NotesMicrophone mic;assert(mic.begin());assert(rail && sampleRate==24000 && clk==19 && data==20);
 unsigned char bytes[256];assert(mic.read(bytes,sizeof(bytes))==256);mic.stop();assert(!rail);assert(mic.read(bytes,sizeof(bytes))==0);assert(mic.close());assert(disables==1 && deletes==1);}
 resetMic();initError=true;
 {NotesMicrophone mic;assert(!mic.begin());assert(!rail);assert(mic.close());assert(deletes==1);}
 resetMic();duringCreate=[](NotesMicrophone* p){p->stop();};
 {NotesMicrophone mic;activeMic=&mic;assert(!mic.begin());assert(!rail);assert(mic.close());assert(deletes==1);}
 puts("Notes actual Sticky PDM adapter rail/pins/24k/stop/init cancellation OK (scripted IDF)");
}
