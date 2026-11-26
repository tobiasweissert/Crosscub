#pragma once
#include "network/NotesDictation.h"

namespace ink {
inline NotesDictation& notesOwner(lua_State* L) {
  return *static_cast<NotesDictation*>(lua_touserdata(L, lua_upvalueindex(1)));
}
inline int notesSnapshotResult(lua_State* L) {
  const auto& s = **static_cast<NotesDictation::Snapshot**>(lua_touserdata(L, 1));
  lua_pushlstring(L, s.text.data(), s.text.size());
  lua_pushstring(L, s.status);
  lua_pushboolean(L, s.busy);
  return 3;
}
inline int notesPoll(lua_State* L) {
  luaL_checkstack(L, 3, "dictation snapshot");
  NotesDictation::Snapshot* active = nullptr;
  lua_pushcfunction(L, notesSnapshotResult);
  lua_pushlightuserdata(L, &active);
  int result;
  {
    auto snapshot = notesOwner(L).snapshot();
    active = &snapshot;
    result = lua_pcall(L, 1, 3, 0);
  }
  // Lua longjmp must not bypass the native snapshot/string destructor on OOM.
  if (result != LUA_OK) return lua_error(L);
  return 3;
}
inline void registerNotesDictation(lua_State* L, NotesDictation& owner) {
  const struct { const char* name; lua_CFunction function; } functions[] = {
    {"notes_dictation_start", [](lua_State* L) { lua_pushboolean(L, notesOwner(L).start()); return 1; }},
    {"notes_dictation_stop", [](lua_State* L) { notesOwner(L).stop(); return 0; }},
    {"notes_dictation_cancel", [](lua_State* L) { notesOwner(L).cancel(); return 0; }},
    {"notes_dictation_poll", notesPoll},
  };
  for (const auto& function : functions) {
    lua_pushlightuserdata(L, &owner);
    lua_pushcclosure(L, function.function, 1);
    lua_setfield(L, -2, function.name);
  }
}
}  // namespace ink
