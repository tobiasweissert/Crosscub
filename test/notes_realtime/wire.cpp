#include <cassert>
#include <cstdio>

#include "network/NotesRealtimeWire.h"
#include "network/ProtectedPaths.h"
using namespace notes;
int main() {
  assert(protectedpaths::isSensitivePath("/.crosspoint/notes-openai.key"));
  assert(!protectedpaths::isPluginPath("/.crosspoint/NOTES-OPENAI.KEY.tmp"));
  WebSocket wire;
  std::string message;
  unsigned opcode = 0;
  const std::string frame = "\x81\x05hello";
  for (char c : frame) assert(wire.feed(&c, 1));
  assert(wire.next(message, opcode) == 1 && opcode == 1 && message == "hello");
  assert(wire.next(message, opcode) == 0);
  const char fragmented[] = {1, 2, 'a', 'b', char(0x89), 1, '?', char(0x80), 2, 'c', 'd'};
  assert(wire.feed(fragmented, sizeof(fragmented)));
  assert(wire.next(message, opcode) == 1 && opcode == 9 && message == "?");
  assert(wire.next(message, opcode) == 1 && opcode == 1 && message == "abcd");
  auto masked = WebSocket::frame("abc", 1, 0x04030201);
  assert(masked.size() == 9 && (unsigned char)masked[1] == 0x83);
  assert(masked.substr(2, 4) == std::string("\1\2\3\4", 4));
  assert(masked[6] == ('a' ^ 1) && masked[7] == ('b' ^ 2) && masked[8] == ('c' ^ 3));
  WebSocket bad;
  const char maskedServer[] = {char(0x81), char(0x80)};
  assert(bad.feed(maskedServer, 2));
  assert(bad.next(message, opcode) == -1);
  WebSocket huge;
  const char hugeHeader[] = {char(0x81), 126, 0x40, 1};
  assert(huge.feed(hugeHeader, 4));
  assert(huge.next(message, opcode) == -1);
  WebSocket invalid;
  const char orphan[] = {char(0x80), 0};
  assert(invalid.feed(orphan, 2));
  assert(invalid.next(message, opcode) == -1);
  assert(validKey("sk-fixture-123") && !validKey("sk-bad\r\nX: evil") && !validKey(""));
  puts("Notes production WebSocket framing/key admission OK");
}
