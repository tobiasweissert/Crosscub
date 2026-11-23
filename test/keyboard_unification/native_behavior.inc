// Actual production KeyboardEntryActivity::loop via SDL -> HalGPIO -> mapped
// input. Private members are observed for assertions only (-fno-access-control).
template<class A> static void mouse(A& a, int x, int y, bool down) {
  SDL_Event event{};
  event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  SDL_PushEvent(&event);
  gpio.beginFrame();
  gpio.update();
  a.loop();
}
static void tapValue(KeyboardEntryActivity& a, int16_t value) {
  a.render(RenderLock{});
  const auto* items = a.interactions.publishedData();
  for (size_t i = 0; i < a.interactions.publishedCount(); ++i) {
    if (items[i].value != value) continue;
    const auto rect = items[i].rect;
    mouse(a, rect.x + rect.width / 2, rect.y + rect.height / 2, true);
    mouse(a, rect.x + rect.width / 2, rect.y + rect.height / 2, false);
    return;
  }
  fprintf(stderr, "missing published native value %d\n", value);
  abort();
}
static void nativeTraversal(bool rtl) {
  std::string text;for(int i=0;i<32;++i)text+=rtl?"אב":"é猫";
  KeyboardEntryActivity a(renderer,mappedInput,rtl?"Neutral RTL":"Neutral long UTF8",text,512);
  a.onEnter();a.render(RenderLock{});
  mouse(a,renderer.getScreenWidth()/2,a.inputStartY()+renderer.getLineHeight(UI_12_FONT_ID)/2,true);
  mouse(a,renderer.getScreenWidth()/2,a.inputStartY()+renderer.getLineHeight(UI_12_FONT_ID)/2,false);
  // Use the retained simulator HAL's held-key script: SDL_PushEvent records
  // edges but does not change SDL_GetKeyboardState (and cannot prove a hold).
  delay(120);gpio.beginFrame();gpio.update();a.loop();
  delay(550);gpio.beginFrame();gpio.update();a.loop();
  delay(200);gpio.beginFrame();gpio.update();a.loop();
  assert(a.cursorMode);
  std::vector<size_t> boundaries{0};for(size_t i=1;i<text.size();++i)if((static_cast<unsigned char>(text[i])&0xc0)!=0x80)boundaries.push_back(i);boundaries.push_back(text.size());
  const auto current=std::find(boundaries.begin(),boundaries.end(),a.cursorPos);assert(current!=boundaries.end());
  for(size_t i=static_cast<size_t>(current-boundaries.begin());i>0;--i){key(a,SDL_SCANCODE_LEFT,true);key(a,SDL_SCANCODE_LEFT,false);assert(a.cursorPos==boundaries[i-1]);a.render(RenderLock{});}
  for(size_t i=1;i<boundaries.size();++i){key(a,SDL_SCANCODE_RIGHT,true);key(a,SDL_SCANCODE_RIGHT,false);assert(a.cursorPos==boundaries[i]);a.render(RenderLock{});}
  assert(a.text==text);printf("NATIVE_TRAVERSAL_PASS rtl=%d codepoints=%zu bidirectional_HAL_cursor UTF8_unchanged\n",rtl,boundaries.size()-1);
}
static void nativeBehavior() {
  namespace fui = freeink::ui;
  // The text viewport begins directly below the header, not at a fixed
  // portrait-only offset. Its first line must be tappable above the number row.
  KeyboardEntryActivity geometry(renderer, mappedInput, "Geometry", "é");
  geometry.onEnter();
  geometry.render(RenderLock{});
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int fieldY = metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) + metrics.verticalSpacing;
  size_t position = 999;
  const auto fieldTarget = geometry.inputFieldTouchTargetFromPoint(renderer.getScreenWidth()/2, fieldY + renderer.getLineHeight(UI_12_FONT_ID)/2, position);
  printf("GEOMETRY fieldY=%d keyboardY=%d target=%d\n",fieldY,geometry.keyboardRect().y,int(fieldTarget));
  fflush(stdout);
  assert(fieldTarget == KeyboardEntryActivity::InputFieldTouchTarget::Cursor);
  assert(fieldY + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing + 8 < geometry.keyboardRect().y);
  geometry.text = std::string(400, 'W') + "é";
  geometry.cursorPos = geometry.text.size();
  geometry.render(RenderLock{});
  position = 0;
  geometry.inputFieldTouchTargetFromPoint(0,fieldY + renderer.getLineHeight(UI_12_FONT_ID)/2,position);
  printf("LONG_VIEWPORT first_visible_byte=%zu\n", position);fflush(stdout);
  assert(position > 0 && position < geometry.text.size());
  assert((static_cast<unsigned char>(geometry.text[position]) & 0xc0) != 0x80);
  // Inherited Enter-up is not a fresh Enter activation.
  KeyboardEntryActivity held(renderer, mappedInput, "Held release", "", 0);
  SDL_Event inherited{};
  inherited.type = SDL_KEYDOWN;
  inherited.key.keysym.scancode = SDL_SCANCODE_RETURN;
  SDL_PushEvent(&inherited);
  gpio.beginFrame();
  gpio.update();
  held.onEnter();
  key(held, SDL_SCANCODE_RETURN, false);
  assert(held.text.empty());
  KeyboardEntryActivity a(renderer, mappedInput, "UTF8 byte cap", "é", 3, InputType::Text, 1);
  a.onEnter();
  tapValue(a, 'q');
  assert(a.text == "éq");
  tapValue(a, 'w');
  assert(a.text == "éq");
  tapValue(a, fui::QWERTY_KEY_BACKSPACE);
  assert(a.text == "é");
  tapValue(a, fui::QWERTY_KEY_BACKSPACE);
  assert(a.text.empty());
  finished = false;
  tapValue(a, fui::QWERTY_KEY_ENTER);
  assert(!finished);
  tapValue(a, 'q');
  tapValue(a, fui::QWERTY_KEY_ENTER);
  assert(finished);
  const auto* result = std::get_if<KeyboardResult>(&a.result.data);
  assert(result && result->text == "q");
  KeyboardEntryActivity cancel(renderer, mappedInput, "Cancel", "kept");
  cancel.onEnter();
  finished = false;
  key(cancel, SDL_SCANCODE_ESCAPE, true);
  key(cancel, SDL_SCANCODE_ESCAPE, false);
  assert(finished && cancel.result.isCancelled);
  KeyboardEntryActivity password(renderer, mappedInput, "Password", "é", 0, InputType::Password);
  password.onEnter();
  assert(password.displayTextForCurrentState() != "é");
  tapValue(password, 'q');
  assert(password.text == "éq" && password.inputType == InputType::Password);
  KeyboardEntryActivity url(renderer, mappedInput, "URL", "https://example.invalid/", 64, InputType::Url);
  url.onEnter();
  tapValue(url, '/');
  assert(url.text == "https://example.invalid//" && url.inputType == InputType::Url);
  KeyboardEntryActivity hold(renderer,mappedInput,"Hold edges","",32);
  hold.onEnter();hold.render(RenderLock{});
  auto holdValue=[&](int16_t value,unsigned duration) {
    hold.render(RenderLock{});bool found=false;
    for(size_t i=0;i<hold.interactions.publishedCount();++i) {
      const auto item=hold.interactions.publishedData()[i];if(item.value!=value)continue;
      const int x=item.rect.x+item.rect.width/2,y=item.rect.y+item.rect.height/2;
      mouse(hold,x,y,true);delay(duration);gpio.beginFrame();gpio.update();hold.loop();mouse(hold,x,y,false);found=true;break;
    }
    assert(found);
  };
  holdValue('q',400);assert(hold.text=="Q");
  holdValue(fui::QWERTY_KEY_BACKSPACE,950);assert(hold.text.empty());
  puts("NATIVE_HOLD_PASS alternate_once delete_clear release_swallowed");
  puts("NATIVE_BEHAVIOR_PASS held_release UTF8 byte_cap min cancel password URL actual_HAL_touch");
}
