extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#include <HalStorage.h>

#include <cassert>
#include <cstdio>

#include "activities/apps/lua/LuaNotesDictation.h"
int main() {
  keyFile.clear();
  auto* L = luaL_newstate();
  luaL_openlibs(L);
  NotesDictation dictation;
  lua_newtable(L);
  ink::registerNotesDictation(L, dictation);
  lua_setglobal(L, "smudge");
  assert(luaL_dostring(L,
                       "assert(smudge.notes_dictation_start()==false); local "
                       "text,status,busy=smudge.notes_dictation_poll(); assert(text=='' and type(status)=='string' and "
                       "busy==false);smudge.notes_dictation_stop();smudge.notes_dictation_cancel()") == LUA_OK);
  assert(luaL_dostring(L,
                       "assert(smudge.notes_key_status()==false);"
                       "assert(smudge.notes_key_save('sk-canary')==true);"
                       "assert(smudge.notes_key_status()==true);"
                       "assert(smudge.notes_key_save('bad\\r\\nheader')==false);"
                       "assert(smudge.notes_key_status()==true);"
                       "assert(smudge.notes_key_clear()==true);"
                       "assert(smudge.notes_key_status()==false)") == LUA_OK);
  assert(keyFile.empty());
  assert(notes::keys::save("old-canary", 10));
  for (bool* failure : {&failWrite, &failSync, &failClose, &failRead}) {
    *failure = true;
    assert(!notes::keys::save("new-canary", 10));
    *failure = false;
    assert(keyFile == "old-canary" && notes::keys::configured());
  }
  failRenameFrom = notes::keys::TEMP;
  assert(!notes::keys::save("new-canary", 10));
  failRenameFrom.clear();
  assert(keyFile == "old-canary");
  failReadPath = notes::keys::PATH;
  assert(!notes::keys::save("new-canary", 10));
  failReadPath.clear();
  assert(keyFile == "old-canary");
  failRenameFrom = notes::keys::TEMP;
  failRollback = true;
  assert(!notes::keys::save("new-canary", 10));
  assert(keyFile.empty() && extraFiles[notes::keys::BACKUP] == "old-canary");
  failRenameFrom.clear();
  failRollback = false;
  assert(notes::keys::configured());
  assert(notes::keys::save("old-canary", 10));
  failRenameFrom = notes::keys::PATH;
  assert(!notes::keys::save("new-canary", 10));
  failRenameFrom.clear();
  assert(keyFile == "old-canary");
  assert(!notes::keys::save("", 0));
  assert(!notes::keys::save("bad!key", 7));
  assert(!notes::keys::save("bad key", 7));
  assert(!notes::keys::save("bad\0key", 7));
  assert(!notes::keys::save(std::string(513, 'x').data(), 513));
  extraFiles[notes::keys::BACKUP] = keyFile;
  keyFile.clear();
  assert(notes::keys::configured());
  assert(notes::keys::save("new-canary", 10));
  assert(keyFile == "new-canary" && !Storage.exists(notes::keys::BACKUP));
  failRemove = notes::keys::PATH;
  assert(!notes::keys::clear() && keyFile == "new-canary");
  failRemove.clear();
  assert(notes::keys::clear() && !notes::keys::configured());
  lua_close(L);
  puts("Notes actual Lua native dictation binding/config failure/poll/cancel OK");
}
