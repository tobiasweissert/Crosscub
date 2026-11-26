#pragma once

#include <FreeInkUIGfxRenderer.h>

#include <cstring>

#include "LuaRuntime.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "fontIds.h"

// Paint only: Lua retains its existing keymap, hit targets, handlers and editor
// ownership. No native text-input or asynchronous modal API is implied.
namespace ink::keyboard {
#if defined(FREEINK_DEVICE_STICKY) && FREEINK_DEVICE_STICKY
inline int paint(lua_State* L) {
  auto* renderer = static_cast<GfxRenderer*>(lua_touserdata(L, lua_upvalueindex(1)));
  const char* label = luaL_checkstring(L, 1);
  const char* kind = luaL_optstring(L, 8, "text");
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  const lua_Integer w = luaL_checkinteger(L, 4);
  const lua_Integer h = luaL_checkinteger(L, 5);
  // Check in Lua's wide integer domain before narrowing for SDK geometry.
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || w > renderer->getScreenWidth() || h > renderer->getScreenHeight() ||
      x > renderer->getScreenWidth() - w || y > renderer->getScreenHeight() - h) {
    return luaL_error(L, "keyboard key outside screen");
  }
  namespace fui = freeink::ui;
  fui::GfxRendererTarget target(*renderer);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  fui::InteractionBuffer<1> interactions;
  const fui::InputSnapshot noInput{};
  fui::Frame<1> frame(target, target.deviceContext(), noInput, interactions);
  fui::KeyGridKey key;
  key.label = label;
  key.enabled = lua_isnoneornil(L, 7) || lua_toboolean(L, 7);
  if (strcmp(kind, "space") == 0) key.kind = fui::KeyKind::Space;
  if (strcmp(kind, "delete") == 0) key.kind = fui::KeyKind::Delete;
  fui::KeyGridProps props;
  props.keys = &key;
  props.rows = 1;
  props.cols = 1;
  props.selectedIndex = lua_toboolean(L, 6) ? 0 : -1;
  props.radius = fui::KeyboardProps{}.keyRadius;
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  fui::keyGrid(frame,
               {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)},
               props);
  return 0;
}
// Called only under lua_pcall: C-compiled Lua errors must not jump past the
// snapshot owner's C++ destructor. No owning C++ locals belong in this emitter.
inline int snapshotResult(lua_State* L) {
  auto& owner = **static_cast<KeyboardEntryActivity**>(lua_touserdata(L, 1));
  const auto& interactions = owner.paintedInteractions();
  lua_newtable(L);
  for (size_t i = 0; i < interactions.publishedCount(); ++i) {
    const auto& item = interactions.publishedData()[i];
    const auto* key = owner.paintedKey(item.value);
    if (!key) continue;
    lua_newtable(L);
    auto integer = [&](const char* name, int value) {
      lua_pushinteger(L, value);
      lua_setfield(L, -2, name);
    };
    auto string = [&](const char* name, const char* value) {
      lua_pushstring(L, value ? value : "");
      lua_setfield(L, -2, name);
    };
    integer("x", item.rect.x);
    integer("y", item.rect.y);
    integer("w", item.rect.width);
    integer("h", item.rect.height);
    string("label", key->label);
    string("output", key->output);
    const char* action = "insert";
    namespace fui = freeink::ui;
    if (item.value == fui::QWERTY_KEY_SHIFT)
      action = "shift";
    else if (item.value == fui::QWERTY_KEY_MODE)
      action = "mode";
    else if (item.value == fui::QWERTY_KEY_LANG)
      action = "language";
    else if (item.value == fui::QWERTY_KEY_BACKSPACE)
      action = "delete";
    else if (item.value == fui::QWERTY_KEY_ENTER)
      action = "done";
    string("action", action);
    lua_rawseti(L, -2, i + 1);
  }
  lua_pushinteger(L, owner.snapshotLayoutIndex(false));
  lua_setfield(L, -2, "layout_index");
  lua_pushinteger(L, owner.snapshotLayoutIndex(true));
  lua_setfield(L, -2, "next_layout_index");
  return 1;
}
inline int snapshot(lua_State* L) {
  auto* renderer = static_cast<GfxRenderer*>(lua_touserdata(L, lua_upvalueindex(1)));
  size_t titleSize = 0, textSize = 0;
  const char* title = luaL_checklstring(L, 1, &titleSize);
  const char* text = luaL_checklstring(L, 2, &textSize);
  const lua_Integer cursor = luaL_checkinteger(L, 3), focus = luaL_optinteger(L, 6, 1),
                    language = luaL_optinteger(L, 9, -1);
  const lua_Integer selection = luaL_optinteger(L, 10, 0), top = luaL_optinteger(L, 12, 0);
  const bool masked = lua_toboolean(L, 11);
  const char* movement = luaL_optstring(L, 13, "");
  if (*movement && strcmp(movement, "up") && strcmp(movement, "down"))
    return luaL_error(L, "invalid keyboard movement");
  if (selection < 0 || selection > static_cast<lua_Integer>(textSize) + 1 || top < 0 ||
      top > renderer->getScreenHeight() / 2)
    return luaL_error(L, "invalid keyboard selection/viewport");
  if (language < -1 || language >= keyboard_layouts::COUNT || titleSize > 256 || textSize > 8192 || cursor < 1 ||
      cursor > static_cast<lua_Integer>(textSize) + 1 || focus < 0 || focus > 56 || memchr(title, 0, titleSize) ||
      memchr(text, 0, textSize))
    return luaL_error(L, "invalid keyboard snapshot");
  const bool touch = !lua_isnoneornil(L, 7), shifted = lua_toboolean(L, 4), symbols = lua_toboolean(L, 5);
  const lua_Integer x = touch ? luaL_checkinteger(L, 7) : 0, y = touch ? luaL_checkinteger(L, 8) : 0;
  if (touch && (x < 0 || y < 0 || x >= renderer->getScreenWidth() || y >= renderer->getScreenHeight())) {
    lua_pushinteger(L, 0);
    return 1;
  }
  if (touch || *movement) {
    lua_Integer position = 0;
    {
      MappedInputManager input(gpio, *renderer);
      KeyboardEntryActivity owner(*renderer, input, std::string(title, titleSize), std::string(text, textSize));
      if (*movement)
        position =
            owner.snapshotVertical(static_cast<size_t>(cursor - 1), symbols, strcmp(movement, "up") == 0 ? -1 : 1,
                                   static_cast<int>(language), masked, static_cast<int>(top)) +
            1;
      else
        position = owner.snapshotTouch(static_cast<size_t>(cursor - 1), symbols, static_cast<int>(x),
                                       static_cast<int>(y), static_cast<int>(language), masked, static_cast<int>(top)) +
                   1;
    }
    lua_pushinteger(L, position);
    return 1;
  }
  // Prepare stack capacity, function and POD argument BEFORE any native owner.
  // Even closure/stack allocation failure here has no native lifetime to skip.
  luaL_checkstack(L, 2, "keyboard snapshot result");
  KeyboardEntryActivity* active = nullptr;
  lua_pushcfunction(L, snapshotResult);
  lua_pushlightuserdata(L, &active);
  int status;
  {
    MappedInputManager input(gpio, *renderer);
    KeyboardEntryActivity owner(*renderer, input, std::string(title, titleSize), std::string(text, textSize));
    active = &owner;
    owner.paintSnapshot(static_cast<size_t>(cursor - 1), shifted, symbols, static_cast<int>(focus - 1),
                        static_cast<int>(language), selection ? static_cast<size_t>(selection - 1) : std::string::npos,
                        masked, static_cast<int>(top));
    status = lua_pcall(L, 1, 1, 0);
  }
  // Propagate only after all native owning objects have been destroyed.
  if (status != LUA_OK) return lua_error(L);
  return 1;
}
inline int metrics(lua_State* L) {
  auto* renderer = static_cast<GfxRenderer*>(lua_touserdata(L, lua_upvalueindex(1)));
  lua_newtable(L);
  lua_pushinteger(L, renderer->getLineHeight(UI_12_FONT_ID));
  lua_setfield(L, -2, "label_height");
  lua_pushliteral(L, "ui12");
  lua_setfield(L, -2, "label_font");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "border");
  return 1;
}
#endif
inline void registerPaint(lua_State* L, GfxRenderer& renderer) {
#if defined(FREEINK_DEVICE_STICKY) && FREEINK_DEVICE_STICKY
  lua_pushlightuserdata(L, &renderer);
  lua_pushcclosure(L, paint, 1);
  lua_setfield(L, -2, "keyboard_key");
  lua_pushlightuserdata(L, &renderer);
  lua_pushcclosure(L, snapshot, 1);
  lua_setfield(L, -2, "keyboard_snapshot");
  lua_pushlightuserdata(L, &renderer);
  lua_pushcclosure(L, metrics, 1);
  lua_setfield(L, -2, "keyboard_metrics");
#else
  (void)L;
  (void)renderer;
#endif
}
}  // namespace ink::keyboard
