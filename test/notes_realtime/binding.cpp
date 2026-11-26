extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <HalStorage.h>
#include "activities/apps/lua/LuaNotesDictation.h"
#include <cassert>
#include <cstdio>
int main(){
 keyFile.clear();
 auto* L=luaL_newstate();luaL_openlibs(L);
 NotesDictation dictation;lua_newtable(L);ink::registerNotesDictation(L,dictation);lua_setglobal(L,"smudge");
 assert(luaL_dostring(L,"assert(smudge.notes_dictation_start()==false); local text,status,busy=smudge.notes_dictation_poll(); assert(text=='' and type(status)=='string' and busy==false);smudge.notes_dictation_stop();smudge.notes_dictation_cancel()") == LUA_OK);
 lua_close(L);puts("Notes actual Lua native dictation binding/config failure/poll/cancel OK");
}
