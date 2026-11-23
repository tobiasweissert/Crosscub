// Complete production numeric owners, real render/loop and SDL touch routing.
static bool numericFamilyEqual(freeink::ui::Rect rect,const char* label) {
  std::array<unsigned char,48000> actual;memcpy(actual.data(),display.getFrameBuffer(),actual.size());
  freeink::ui::GfxRendererTarget target(renderer);target.setFont(freeink::ui::GfxRendererTarget::FONT_BODY,UI_12_FONT_ID);
  freeink::ui::InteractionBuffer<1> interactions;freeink::ui::Frame<1> frame(target,target.deviceContext(),{},interactions);
  freeink::ui::KeyGridKey key;key.label=label;
  freeink::ui::KeyGridProps props;props.keys=&key;props.rows=1;props.cols=1;props.selectedIndex=-1;
  props.radius=freeink::ui::KeyboardProps{}.keyRadius;props.labelText.font=freeink::ui::GfxRendererTarget::FONT_BODY;
  renderer.fillRect(rect.x,rect.y,rect.width,rect.height,false);freeink::ui::keyGrid(frame,rect,props);
  bool equal=memcmp(actual.data(),display.getFrameBuffer(),actual.size())==0;
  memcpy(display.getFrameBuffer(),actual.data(),actual.size());return equal;
}
template<class B> static freeink::ui::Rect numericRect(const B& buffer,int value) {
 for(size_t i=0;i<buffer.publishedCount();++i){const auto item=buffer.publishedData()[i];if(item.action==1 && item.value==value)return item.rect;}
 assert(false && "missing actual numeric interaction");return {};
}
template<class A,class B> static void numericTap(A& owner,const B& buffer,int value) {
 owner.render(RenderLock{});const auto r=numericRect(buffer,value);mouse(owner,r.x+r.width/2,r.y+r.height/2,true);mouse(owner,r.x+r.width/2,r.y+r.height/2,false);
}
static bool numericBehavior(const std::string& mode) {
 if(mode=="numeric-percent" || mode=="numeric-page") {
  const bool page=mode=="numeric-page";
  std::unique_ptr<EpubReaderPercentSelectionActivity> a;
  if(page)a=std::make_unique<EpubReaderPercentSelectionActivity>(renderer,mappedInput,uint32_t(0),uint32_t(500));
  else a=std::make_unique<EpubReaderPercentSelectionActivity>(renderer,mappedInput,0.0f);
  a->onEnter();a->render(RenderLock{});
  auto& buffer=a->app.interactions_;
  bool equal=numericFamilyEqual(numericRect(buffer,1),"1");
  numericTap(*a,buffer,1);
  if(!page){numericTap(*a,buffer,-1);numericTap(*a,buffer,-1);}
  numericTap(*a,buffer,2);
  finished=false;numericTap(*a,buffer,-2);assert(finished);
  if(page){const auto* r=std::get_if<PageResult>(&a->result.data);assert(r && r->page==12);}
  else {const auto* r=std::get_if<PercentResult>(&a->result.data);assert(r && std::abs(r->percent-1.2f)<0.001f);}
  a->render(RenderLock{});
  printf("NUMERIC_PASS %s actual_HAL_digits decimal_mode confirm family_equal=%d\n",mode.c_str(),equal);return equal;
 }
 FrontlightTimePickerActivity a(renderer,mappedInput,StrId::STR_OK,8*60);
 a.onEnter();a.activeField=mode=="numeric-hour"?FrontlightTimePickerActivity::Field::Hour:FrontlightTimePickerActivity::Field::Minute;
 a.render(RenderLock{});auto& buffer=a.keyboardInteractions;
 bool equal=numericFamilyEqual(numericRect(buffer,1),"1");
 numericTap(a,buffer,1);numericTap(a,buffer,-1);assert(a.numericEntryDigits==0);
 numericTap(a,buffer,1);numericTap(a,buffer,2);
 if(mode=="numeric-hour")assert(a.hour12==12);else assert(a.minute==12);
 a.render(RenderLock{});
 printf("NUMERIC_PASS %s actual_HAL_digits backspace family_equal=%d\n",mode.c_str(),equal);return equal;
}
