// Exact initiating handlers + real child classes. Persistence/filter/transport
// continuations below are neutral observation seams, NOT navigation/storage proof.
#include <Memory.h>
#include <FsHelpers.h>
#include <Utf8.h>
#include <HttpHeader.h>
#include <functional>
#include "OpdsServerStore.h"
#include "SettingsList.h"
#include "util/BookmarkFile.h"
#include "activities/network/WifiSelectionActivity.h"
#undef SETTINGS
#undef KOREADER_STORE
#define SETTINGS settings
#define KOREADER_STORE store
#include "caller_constants.inc"
struct BoundarySettings:CrossPointSettings {
 unsigned saves=0;bool saveToFile(){++saves;return true;}
 std::string getEffectiveDeviceName(){return "Neutral device";}
};
struct BoundaryStore {
 std::string username,password,url;std::array<HttpHeader,2> headers{};unsigned saves=0;
 const std::string& getUsername(){return username;}const std::string& getPassword(){return password;}const std::string& getServerUrl(){return url;}
 void setCredentials(std::string u,std::string p){username=u;password=p;}void setServerUrl(std::string v){url=v;}
 auto& getCustomHeaders(){return headers;}void setCustomHeader(size_t i,std::string n,std::string v){headers[i]={n,v};}
 bool saveToFile(){++saves;return true;}
};
struct CallerBoundary {
 GfxRenderer& renderer=::renderer;MappedInputManager& mappedInput=::mappedInput;
 std::unique_ptr<Activity> child;std::function<void(const ActivityResult&)> callback;
 BoundarySettings settings;BoundaryStore store;OpdsServer editServer{};
 struct{int selected=0;}nav;int selectedIndex=0;
 static constexpr int CUSTOM_HEADER_1_INDEX=4,CUSTOM_HEADER_2_INDEX=5;

 enum class State{SEARCH_INPUT,BROWSING};
 // Wifi enum and Catalog enum are distinct in production; extract wifi methods
 // into a separate boundary below rather than changing their algorithms.
 State state=State::BROWSING;
 std::string query="",searchResult,renameResult,epubPath="neutral.epub";
 unsigned saves=0,filters=0,viewports=0,updates=0;
 std::vector<BookmarkEntry> bookmarks{BookmarkEntry{}};
 struct{void clearTapFlash(){}}app;
 StrId menuNames[11]={StrId::STR_NONE_OPT,StrId::STR_NONE_OPT,StrId::STR_NONE_OPT,StrId::STR_NONE_OPT,StrId::STR_NONE_OPT,StrId::STR_NONE_OPT,StrId::STR_NONE_OPT,StrId::STR_NONE_OPT,StrId::STR_NONE_OPT,StrId::STR_CUSTOM_HEADER_1,StrId::STR_CUSTOM_HEADER_2};
 template<class A,class F>void startActivityForResult(std::unique_ptr<A> value,F fn){child=std::move(value);callback=fn;}
 template<class A,class F>void openDialog(std::unique_ptr<A> value,F fn){startActivityForResult(std::move(value),fn);}
 void requestUpdate(){++updates;}void fail(StrId){assert(false);}std::string searchPrefill(){return query;}
 void performSearch(std::string value){searchResult=value;}void applyFilter(){++filters;}void resetViewport(){++viewports;}
 bool saveServer(){++saves;return true;}int listCount(){return bookmarks.size();}void rebuildBookmarkRowItems(){}
 bool neutralBookmarkSave(const std::string&,const std::vector<BookmarkEntry>&){++saves;return true;}
 void renameFile(const std::string& p,const std::string& e,const std::string& stem,const std::string& ext){assert(p=="/neutral.epub" && e=="neutral.epub" && ext==".epub");renameResult=stem;++saves;}
 void catalog();void library();void koUsername();void koPassword();void koUrl();void koHeader();
 void opdsName();void opdsUrl();void opdsUsername();void opdsPassword();void opdsHeader();void opdsFolder();
 void startRenameFile(const std::string&,const std::string&);void startRename();void openStringEditor(const SettingInfo&);
 void openFrontlightScheduleTimePicker(uint16_t CrossPointSettings::*,StrId);
};
struct WifiBoundary:CallerBoundary {
 WifiSelectionState state=WifiSelectionState::NETWORK_LIST;std::string selectedSSID,enteredPassword;bool selectedRequiresPassword=false,usedSavedPassword=true,autoConnecting=true;
 void wifiPassword();void wifiSsid();
};
#include "caller_extracted.inc"
#undef SETTINGS
#undef KOREADER_STORE
struct CallerCase {const char* id;void(CallerBoundary::*begin)();StrId title;size_t max;InputType type;};
static void captureNative(const char* id){lua_State* L=luaL_newstate();lua_pushstring(L,id);fixtureCapture(L);lua_close(L);}
static void wifiBehavior(const std::string& id){
 WifiBoundary c;const bool ssid=id=="native.wifi.hidden-ssid";
 auto begin=[&](){if(ssid)c.wifiSsid();else c.wifiPassword();};begin();
 auto& owner=*static_cast<KeyboardEntryActivity*>(c.child.get());assert(owner.title==(ssid?tr(STR_ENTER_WIFI_SSID):tr(STR_ENTER_WIFI_PASSWORD)) && owner.text.empty() && owner.inputType==InputType::Text && owner.maxLength==(ssid?32:64) && owner.minLength==0);
 owner.onEnter();owner.render(RenderLock{});captureNative(id.c_str());finished=false;tapValue(owner,'1');tapValue(owner,fui::QWERTY_KEY_ENTER);assert(finished && !owner.result.isCancelled);c.callback(owner.result);
 assert((ssid?c.selectedSSID:c.enteredPassword)=="1");
 begin();auto& cancel=*static_cast<KeyboardEntryActivity*>(c.child.get());cancel.onEnter();finished=false;key(cancel,SDL_SCANCODE_ESCAPE,true);key(cancel,SDL_SCANCODE_ESCAPE,false);assert(finished && cancel.result.isCancelled);c.callback(cancel.result);assert(c.state==WifiSelectionState::NETWORK_LIST);
 printf("CALLER_PASS %s actual_initiator title_type_limits_default HAL_result=1 cancel=network_list no_network_execution\n",id.c_str());
}
static void scheduleBehavior(const std::string& id){
 CallerBoundary c;const bool end=id.find("end-")!=std::string::npos,minute=id.find("minute")!=std::string::npos;
 const auto member=end?&CrossPointSettings::frontlightScheduleEnd:&CrossPointSettings::frontlightScheduleStart;
 const StrId title=end?StrId::STR_END:StrId::STR_START;
 c.settings.*member=8*60+7;
 c.openFrontlightScheduleTimePicker(member,title);
 auto& owner=*static_cast<FrontlightTimePickerActivity*>(c.child.get());assert(owner.titleId==title && owner.initialTimeOfDay==487);
 owner.onEnter();assert(owner.hour12==8 && owner.minute==7 && !owner.isPm);
 if(minute){key(owner,SDL_SCANCODE_RIGHT,true);key(owner,SDL_SCANCODE_RIGHT,false);}assert(owner.activeField==(minute?FrontlightTimePickerActivity::Field::Minute:FrontlightTimePickerActivity::Field::Hour));
 owner.render(RenderLock{});captureNative(id.c_str());numericTap(owner,owner.keyboardInteractions,1);numericTap(owner,owner.keyboardInteractions,2);finished=false;key(owner,SDL_SCANCODE_RETURN,true);key(owner,SDL_SCANCODE_RETURN,false);assert(finished && !owner.result.isCancelled);
 const unsigned expected=minute?492:7;assert(std::get<IntervalResult>(owner.result.data).value==expected);c.callback(owner.result);assert(c.settings.*member==expected && c.settings.saves==1);
 c.openFrontlightScheduleTimePicker(member,title);auto& cancel=*static_cast<FrontlightTimePickerActivity*>(c.child.get());cancel.onEnter();finished=false;key(cancel,SDL_SCANCODE_ESCAPE,true);key(cancel,SDL_SCANCODE_ESCAPE,false);assert(finished && cancel.result.isCancelled);c.callback(cancel.result);assert(c.settings.*member==expected && c.settings.saves==1);
 printf("CALLER_PASS %s actual_schedule_ctor default=487 real_HAL_field_digits result=%u cancel_no_save title=%d\n",id.c_str(),expected,int(title));
}
static void callerBehavior(const std::string& id){
 if(id.rfind("native.wifi.",0)==0){wifiBehavior(id);return;}if(id.rfind("native.frontlight.",0)==0){scheduleBehavior(id);return;}
 CallerBoundary c;for(size_t i=0;i<2;++i){c.store.headers[i]={"X-Neutral","seed"};c.editServer.customHeaders[i]={"X-Neutral","seed"};}std::string initial,expected;size_t maximum=0,minimum=0;InputType type=InputType::Text;std::string title;
 auto begin=[&](){
  if(id=="native.catalog.opds-search" || id=="native.catalog.plugin-search"){c.catalog();title=tr(STR_SEARCH);}
  else if(id=="native.library.search"){c.library();title=tr(STR_SEARCH);maximum=48;}
  else if(id=="native.file.rename"){c.startRenameFile("/neutral.epub","neutral.epub");title=tr(STR_RENAME);initial="neutral";maximum=494;minimum=1;}
  else if(id=="native.bookmark.rename"){c.startRename();title=tr(STR_RENAME);maximum=BookmarkEntry::MAX_NAME_LENGTH;}
  else if(id=="native.settings.device-name"){
   auto setting=SettingInfo::String(StrId::STR_DEVICE_NAME,c.settings.deviceName,sizeof(c.settings.deviceName),"deviceName",StrId::STR_CAT_SYSTEM);setting.stringOffset=reinterpret_cast<char*>(c.settings.deviceName)-reinterpret_cast<char*>(static_cast<CrossPointSettings*>(&c.settings));c.openStringEditor(setting);title=tr(STR_DEVICE_NAME);initial="Neutral device";maximum=20;minimum=CrossPointSettings::MIN_DEVICE_NAME_LENGTH;
  }
  else if(id=="native.opds.download-folder"){c.opdsFolder();title=tr(STR_OPDS_DOWNLOAD_FOLDER);maximum=63;}
  else {
   static const CallerCase cases[]={
    {"native.koreader.username",&CallerBoundary::koUsername,StrId::STR_KOREADER_USERNAME,64,InputType::Text},
    {"native.koreader.password",&CallerBoundary::koPassword,StrId::STR_KOREADER_PASSWORD,64,InputType::Password},
    {"native.koreader.server-url",&CallerBoundary::koUrl,StrId::STR_SYNC_SERVER_URL,128,InputType::Url},
    {"native.koreader.header1",&CallerBoundary::koHeader,StrId::STR_CUSTOM_HEADER_1,160,InputType::Text},
    {"native.koreader.header2",&CallerBoundary::koHeader,StrId::STR_CUSTOM_HEADER_2,160,InputType::Text},
    {"native.opds.server-name",&CallerBoundary::opdsName,StrId::STR_SERVER_NAME,63,InputType::Text},
    {"native.opds.server-url",&CallerBoundary::opdsUrl,StrId::STR_OPDS_SERVER_URL,127,InputType::Url},
    {"native.opds.username",&CallerBoundary::opdsUsername,StrId::STR_USERNAME,63,InputType::Text},
    {"native.opds.password",&CallerBoundary::opdsPassword,StrId::STR_PASSWORD,63,InputType::Text},
    {"native.opds.header1",&CallerBoundary::opdsHeader,StrId::STR_CUSTOM_HEADER_1,160,InputType::Text},
    {"native.opds.header2",&CallerBoundary::opdsHeader,StrId::STR_CUSTOM_HEADER_2,160,InputType::Text}};
   bool found=false;for(const auto& spec:cases)if(id==spec.id){found=true;c.selectedIndex=id.back()=='2'?10:9;c.nav.selected=id.back()=='2'?5:4;(c.*spec.begin)();title=I18N.get(spec.title);maximum=spec.max;type=spec.type;if(type==InputType::Url)initial="https://";if(id.find("header")!=std::string::npos)initial="X-Neutral: seed";break;}assert(found);
  }
 };
 begin();auto& owner=*static_cast<KeyboardEntryActivity*>(c.child.get());
 assert(owner.title==title && owner.text==initial && owner.maxLength==maximum && owner.minLength==minimum && owner.inputType==type);
 owner.onEnter();owner.render(RenderLock{});captureNative(id.c_str());
 // Editing and completion are always dispatched by real HAL to the whole owner.
 finished=false;tapValue(owner,'1');assert(owner.text==initial+"1");tapValue(owner,fui::QWERTY_KEY_ENTER);assert(finished && !owner.result.isCancelled);
 expected=initial+"1";c.callback(owner.result);
 if(id.find("catalog.")!=std::string::npos)assert(c.searchResult==expected);
 else if(id=="native.library.search")assert(c.query==expected && c.filters==1 && c.viewports==1);
 else if(id=="native.file.rename")assert(c.renameResult==expected && c.saves==1);
 else if(id=="native.bookmark.rename")assert(c.bookmarks[0].name==expected && c.saves==1);
 else if(id=="native.settings.device-name")assert(c.settings.deviceName==expected && c.settings.saves==1);
 else if(id=="native.opds.download-folder")assert(std::string(c.settings.opdsDownloadFolder)=="/1" && c.settings.saves==1);
 else if(id=="native.koreader.username")assert(c.store.username==expected && c.store.saves==1);
 else if(id=="native.koreader.password")assert(c.store.password==expected && c.store.saves==1);
 else if(id=="native.koreader.server-url")assert(c.store.url==expected && c.store.saves==1);
 else if(id=="native.opds.server-name")assert(c.editServer.name==expected && c.saves==1);
 else if(id=="native.opds.server-url")assert(c.editServer.url==expected && c.saves==1);
 else if(id=="native.opds.username")assert(c.editServer.username==expected && c.saves==1);
 else if(id=="native.opds.password")assert(c.editServer.password==expected && c.saves==1);
 else {assert(id.find("header")!=std::string::npos);const size_t slot=id.back()=='2'?1:0;const auto& h=id.find("koreader")!=std::string::npos?c.store.headers[slot]:c.editServer.customHeaders[slot];assert(h.name=="X-Neutral" && h.value=="seed1");assert(c.store.saves==1 || c.saves==1);}
 // Actual Back cancellation and the original captured closure must not save.
 begin();auto& cancel=*static_cast<KeyboardEntryActivity*>(c.child.get());cancel.onEnter();finished=false;key(cancel,SDL_SCANCODE_ESCAPE,true);key(cancel,SDL_SCANCODE_ESCAPE,false);assert(finished && cancel.result.isCancelled);
 const unsigned before=c.saves+c.store.saves+c.settings.saves;c.callback(cancel.result);assert(before==c.saves+c.store.saves+c.settings.saves);
 printf("CALLER_PASS %s title=%s type=%d max=%zu min=%zu default=%s result=%s cancel_no_save=1 boundary=extracted_initiator_real_child_HAL_original_result_closure\n",id.c_str(),title.c_str(),int(type),maximum,minimum,initial.c_str(),expected.c_str());
}
