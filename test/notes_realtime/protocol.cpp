#include "network/NotesRealtimeProtocol.h"
#include <cassert>
#include <cstdio>
int main(){
 notes::Transcript t;
 assert(t.consume(R"({"type":"session.updated"})") && t.ready);
 assert(t.consume(R"({"type":"conversation.item.input_audio_transcription.delta","item_id":"a","content_index":0,"delta":"Hello "})"));
 assert(t.text=="Hello " && !t.complete);
 assert(t.consume(R"({"type":"conversation.item.input_audio_transcription.delta","item_id":"a","content_index":0,"delta":"world"})"));
 assert(t.text=="Hello world");
 assert(t.consume(R"({"type":"conversation.item.input_audio_transcription.completed","item_id":"a","content_index":0,"transcript":"Hello, world!"})"));
 assert(!t.complete && t.text=="Hello, world!");
 assert(t.consume(R"({"type":"input_audio_buffer.committed","item_id":"a"})") && t.complete);
 assert(!t.consume("not JSON"));
 notes::Transcript mismatch;
 assert(mismatch.consume(R"({"type":"input_audio_buffer.committed","item_id":"b"})"));
 assert(!mismatch.consume(R"({"type":"conversation.item.input_audio_transcription.delta","item_id":"a","content_index":0,"delta":"oops"})"));
 notes::Transcript invalid;
 assert(!invalid.consume(R"({"type":"conversation.item.input_audio_transcription.delta","item_id":"a","content_index":0,"delta":"\u0000"})"));
 notes::Transcript limits;
 std::string huge(3073,'x');
 assert(!limits.consume("{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"a\",\"content_index\":0,\"delta\":\""+huge+"\"}"));
 assert(!notes::validText(std::string("\xc0\xaf",2)));
 assert(!notes::validText(std::string(97,'\n')));
 assert(notes::validText("Grüezi\n你好"));
 assert(std::string(notes::SESSION_UPDATE).find("24000")!=std::string::npos);
 assert(std::string(notes::SESSION_UPDATE).find("gpt-live-transcribe")!=std::string::npos);
 puts("Notes actual JSON live transcript delta/final/item matching/bounds OK");
}
