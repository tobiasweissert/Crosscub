// Bounded extracted production keypad paint/input/result; NOT reader lifecycle.
#include "activities/reader/EpubReaderTouchMenuActivity.h"
#include "activities/reader/EpubReaderMenuActivity.h"
#include "components/UiAppHelpers.h"
#include "components/icons/keyboardIcons.h"
#include "components/icons/listIcons.h"
#include "components/icons/touchHeaderIcons.h"
#include "components/DrawerHandle.h"
#include "activities/reader/StablePageSelectionModel.h"
namespace fui=freeink::ui;
#include "drawer_constants.inc"
struct DrawerBoundary {
 using UiApp=fui::FreeInkApp<48,11>;
 static constexpr fui::ActionId ACTION_KEYPAD_KEY=10,ACTION_KEYPAD_BACKSPACE=11;
 static constexpr fui::ActionId ACTION_DISMISS=3,ACTION_TAB=2,ACTION_BACK=4;
 GfxRenderer& renderer=::renderer;fui::Rect drawerHandleRect{};StrId enumOptionTitle=StrId::STR_NONE_OPT;
 int percent=4300; uint32_t stablePage=43,stablePageCount=500;
 const int percentSeed=4300; const uint32_t stablePageSeed=43;
 char entryText[8]={}; uint8_t entryLen=0;bool buttonFocusActive=false,didChangeSettings=false;
 ReaderDrawerState state{};ReaderSettingsDraft draft{};ReaderSettingsChangeMask changeMask=ReaderSettingsChangeMask::None;
 std::array<fui::KeyGridKey,12> keypadKeys{};
 fui::GfxRendererTarget uiTarget;UiApp app;MenuResult result{};bool done=false;unsigned commits=0;
 DrawerBoundary():uiTarget(makeUiTarget(renderer)),app(uiTarget,uiTarget.deviceContext()) {
  applySharedUiTheme(app,uiTarget);app.on(ACTION_KEYPAD_KEY,onKeypadKeyEvent,this);app.on(ACTION_KEYPAD_BACKSPACE,onKeypadBackspaceEvent,this);
  app.setScreen([](UiApp::ScreenType& screen,void* user){static_cast<DrawerBoundary*>(user)->buildDrawer(screen);},this);
 }
 void requestUpdate(){} // scheduling seam only
 void commitSettings(){++commits;assert(!didChangeSettings);} // neutral settings, no storage
 void setResult(MenuResult&& value){result=std::move(value);}void finish(){done=true;}

 void render(RenderLock&&){renderer.clearScreen();app.setDevice(uiTarget.deviceContext());app.render();}
 void loop(){auto snap=touchSnapshotFrom(mappedInput);snap.touchPressed=false;app.route(snap);}
 void buildDrawerKeypad(UiApp::ScreenType&,bool,const char*);
 int16_t drawerHeight()const;const char* paneTitle()const;
 void buildDrawer(UiApp::ScreenType&);void buildTabBar(UiApp::ScreenType&,fui::Rect,bool);void buildPaneHeader(UiApp::ScreenType&);void buildPercentPane(UiApp::ScreenType&);void buildStablePagePane(UiApp::ScreenType&);
 void buildRootRows(UiApp::ScreenType&){std::abort();}void buildSpacingPane(UiApp::ScreenType&){std::abort();}void buildMarginsPane(UiApp::ScreenType&){std::abort();}void buildAutoPageTurnPane(UiApp::ScreenType&){std::abort();}void buildDictionaryPane(UiApp::ScreenType&){std::abort();}void buildFontFamilyPane(UiApp::ScreenType&){std::abort();}void buildEnumOptionsPane(UiApp::ScreenType&){std::abort();}void buildSimplePane(UiApp::ScreenType&){std::abort();}
 void resetKeypadEntry();void appendKeypadDigit(char);void appendKeypadDecimalPoint();void backspaceKeypadEntry();void syncKeypadValue();
 static void onKeypadKeyEvent(const fui::ActionEvent&,void*);static void onKeypadBackspaceEvent(const fui::ActionEvent&,void*);
 void completePercentSelection();void completeStablePageSelection();
};
#include "drawer_extracted.inc"
// Same persistent actual SDK target: production header plus a non-keypad SDK
// setting row. This observes configured typography, not private font storage.
static void drawerFontControl(DrawerBoundary::UiApp::ScreenType& screen,void* user) {
 auto& owner=*static_cast<DrawerBoundary*>(user);owner.buildPaneHeader(screen);
 fui::SettingRowProps row;row.label="Body control 123";row.value="Value 43";
 row.labelText=screen.theme().bodyText;row.valueText=screen.theme().smallText;
 screen.settingRow(row);
}
static void drawerFontBehavior(bool page,bool large) {
 SETTINGS.uiScale=large?CrossPointSettings::UI_SCALE_LARGE:CrossPointSettings::UI_SCALE_SMALL;
 DrawerBoundary owner;owner.state.pane=page?ReaderDrawerPane::StablePage:ReaderDrawerPane::Percent;
 const auto metrics=[&](){std::array<int16_t,8> result{};for(unsigned slot=0;slot<4;++slot){
  result[slot*2]=owner.uiTarget.measureText(slot,"Body control 123",{}).width;
  result[slot*2+1]=owner.uiTarget.lineHeight(slot);
 }return result;};
 const auto control=[&](){owner.app.setScreen(drawerFontControl,&owner);owner.render(RenderLock{});
  return std::vector<uint8_t>(display.getFrameBuffer(),display.getFrameBuffer()+display.getBufferSize());};
 const auto baselineMetrics=metrics();const auto baselinePixels=control();
 assert(baselineMetrics[2]==renderer.getTextWidth(large?UI_12_FONT_ID:UI_10_FONT_ID,"Body control 123"));
 bool preserved=true;
 for(int repaint=0;repaint<3;++repaint){
  owner.app.setScreen([](DrawerBoundary::UiApp::ScreenType& screen,void* user){static_cast<DrawerBoundary*>(user)->buildDrawer(screen);},&owner);
  owner.render(RenderLock{});
  fui::Rect keyRect{};for(size_t i=0;i<owner.app.interactions_.publishedCount();++i){auto item=owner.app.interactions_.publishedData()[i];if(item.action==10 && item.value==1)keyRect=item.rect;}
  assert(!keyRect.empty());const bool family=numericFamilyEqual(keyRect,"1");
  const bool sameMetrics=metrics()==baselineMetrics;const bool samePixels=control()==baselinePixels;
  printf("DRAWER_FONT_OBSERVATION page=%d large=%d repaint=%d metrics=%d header_control_pixels=%d UI12_key_family=%d body_width=%d baseline_width=%d\n",page,large,repaint,sameMetrics,samePixels,family,metrics()[2],baselineMetrics[2]);
  preserved=preserved && sameMetrics && samePixels && family;
 }
 fflush(stdout);assert(preserved && "KB-R2-01 configured font metrics/header/non-keypad pixels changed");
 printf("DRAWER_FONT_PASS page=%d large=%d persistent_target three_repaints all_font_slots actual_production_header SDK_setting_row UI12_keys\n",page,large);
}
static void drawerBehavior(bool page) {
 DrawerBoundary owner;owner.state.pane=page?ReaderDrawerPane::StablePage:ReaderDrawerPane::Percent;
 owner.render(RenderLock{});
 auto rect=[&](int value,int action=10){for(size_t i=0;i<owner.app.interactions_.publishedCount();++i){auto item=owner.app.interactions_.publishedData()[i];if(item.action==action && item.value==value)return item.rect;}assert(false);return fui::Rect{};};
 const bool equal=numericFamilyEqual(rect(1),"1");printf("DRAWER_FAMILY_EQUAL=%d\n",equal);fflush(stdout);assert(equal);
 auto tap=[&](int value,int action=10){owner.render(RenderLock{});auto r=rect(value,action);mouse(owner,r.x+r.width/2,r.y+r.height/2,true);mouse(owner,r.x+r.width/2,r.y+r.height/2,false);};
 tap(1);if(!page)tap(-1);tap(2);assert(std::string(owner.entryText)==(page?"12":"1.2"));
 tap(0,11);assert(std::string(owner.entryText)==(page?"1":"1."));tap(2);tap(-2);assert(owner.done && owner.commits==1);
 if(page)assert(owner.result.drawerPage==12);else assert(owner.result.drawerValue==120);
 owner.resetKeypadEntry();assert(owner.percent==4300 && owner.stablePage==43 && owner.entryLen==0);
 owner.render(RenderLock{});printf("DRAWER_PASS page=%d actual_extracted_render_HAL_SDK_result reset no_reader_navigation\n",page);
}
