#include "network/NotesAudioQueue.h"
#include <cassert>
#include <cstdio>
int main(){
 unsigned char memory[8]; notes::AudioQueue q(memory,8); unsigned char a[]={1,2,3,4,5,6},out[8];
 assert(q.push(a,6));assert(!q.push(a,4));assert(q.pop(out,4)==4 && out[0]==1 && out[3]==4);
 assert(q.push(a,6));assert(q.pop(out,8)==8 && out[0]==5 && out[1]==6 && out[2]==1 && out[7]==6);
 assert(!q.push(a,3));assert(q.pop(out,8)==0);
 puts("Notes bounded PCM16 queue wrap/overflow/alignment OK");
}
