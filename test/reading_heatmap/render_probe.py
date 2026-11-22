#!/usr/bin/env python3
"""Execute the real heatmap render function against draw-call geometry spies, not screenshots."""
from pathlib import Path
import argparse, os, subprocess, tempfile
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
cpp=(R/'src/activities/reader/BookStatsView.cpp').read_text()
render=cpp[cpp.index('void renderReadingHeatmapPage('):cpp.index('void renderGlobalStatsPage(')]
harness=r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "ReadingHeatmap.h"
#include "RecentReadingStats.h"
constexpr int UI_10_FONT_ID=10,UI_12_FONT_ID=12,SMALL_FONT_ID=8;
namespace EpdFontFamily {constexpr int BOLD=1;}
struct GfxRenderer {
 struct Rect {int x,y,w,h;};struct Text {int x,y;std::string value;};
 int width,height;std::vector<Rect> fills,frames;std::vector<Text> texts;std::vector<std::pair<int,int>> pixels;
 void clearScreen(){}int getScreenWidth()const{return width;}int getScreenHeight()const{return height;}
 int getLineHeight(int)const{return 22;}int getTextWidth(int f,const char* s)const{return std::string(s).size()*(f==SMALL_FONT_ID?8:12);}
 void drawText(int,int x,int y,const char* s,bool=true,int=0){texts.push_back({x,y,s});}
 void fillRect(int x,int y,int w,int h,bool){fills.push_back({x,y,w,h});}
 void drawRect(int x,int y,int w,int h,bool){frames.push_back({x,y,w,h});}
 void drawPixel(int x,int y,bool){pixels.push_back({x,y});}
 template<class B>bool drawBitmap(B&,int x,int y,int w,int h){frames.push_back({x,y,w,h});return true;}
 std::vector<std::string> wrappedText(int,const char*,int,int,int=0);
 std::string truncatedText(int f,const char* s,int w){std::string t(s);int n=w/(f==SMALL_FONT_ID?8:12);return int(t.size())>n?t.substr(0,n-3)+"...":t;}

};
std::vector<std::string> GfxRenderer::wrappedText(int f,const char* s,int w,int n,int){int charW=f==SMALL_FONT_ID?8:12;std::string t(s);std::vector<std::string> out;while(!t.empty() && int(out.size())<n){out.push_back(t.substr(0,w/charW));t=t.substr(std::min(t.size(),size_t(w/charW)));}return out;}
struct MappedInputManager {bool hasTouchHardware()const{return true;}std::string withBackArrow(const char* s)const{return s;}
 struct Labels {const char* btn1="",*btn2="",*btn3="",*btn4="";};template<class... Args>Labels mapLabels(Args...)const{return {};}};
struct TouchHeaderBackButton {static void drawCompact(GfxRenderer&,const char*,bool){}};
struct CompactHeader {static void drawTitle(GfxRenderer&,const char*){}};
struct Metrics {int contentSidePadding=20,topPadding=16,homeCoverHeight=370,buttonHintsHeight=36,verticalSpacing=10;};
struct UITheme {static std::string getCoverThumbPath(const std::string& p,int,int){return p;}static std::string getCoverThumbPath(const std::string& p,int){return p;}static UITheme& getInstance(){static UITheme t;return t;}const Metrics& getMetrics(){static Metrics m;return m;}};
struct Layout {int topGap=12;};
Layout getStatsLayout(GfxRenderer&,const MappedInputManager*,bool,bool,bool){return {};}
int statsHeaderHeight(const Metrics&,const Layout&,const MappedInputManager*){return 48;}
struct Gui {template<class... Args>void drawButtonHints(Args...){}} GUI;
constexpr int STR_EXIT=0,STR_BACK=1,STR_MORE=2;
const char* tr(int){return "";}
struct GlobalReadingStats {uint32_t readingHistoryAnchorDay=0;std::array<uint8_t,READING_HISTORY_BYTES> readingHistoryBits{};};
bool validClock=true;
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime& now){now=validClock?ReadingStatsDateTime{{2024,3,1},12,0,0}:ReadingStatsDateTime{};return validClock;}
'''
harness+=r'''
struct Rect {int x,y,width,height;};
std::vector<Rect> placeholders; std::vector<std::string> opened;
struct HalFile {std::string path;void close(){}};
struct StorageDouble {bool openFileForRead(const char*,const std::string& p,HalFile& f){opened.push_back(p);f.path=p;return p=="cached-A" || p=="corrupt";}} Storage;
enum class BmpReaderError {Ok,Invalid};
struct Bitmap {HalFile& file;Bitmap(HalFile& f):file(f){} BmpReaderError parseHeaders(){return file.path=="corrupt"?BmpReaderError::Invalid:BmpReaderError::Ok;}int getWidth(){return 80;}int getHeight(){return 120;}};
struct BaseTheme {static void drawCoverPlaceholder(GfxRenderer&,Rect r){placeholders.push_back(r);}};
struct BookReadingStats {static void formatDuration(uint32_t n,char* b,size_t s){snprintf(b,s,"%u min",n/60);}};
'''
harness+=render+r'''
int main(){
 for(int width:{480,800}){
  GlobalReadingStats stats;const auto today=readingStatsDayIndex({2024,3,1});
  for(unsigned n=0;n<365;++n)markReadingHistoryDay(stats.readingHistoryAnchorDay,stats.readingHistoryBits,today-n);
  auto before=stats.readingHistoryBits;MappedInputManager input;
  GfxRenderer r{width,width==480?800:480,{},{},{},{}};renderReadingHeatmapPage(r,&input,stats,true,{});
  std::set<int> rows;for(const auto& c:r.fills)rows.insert(c.y);
  assert(rows.size()==7); // Exactly one continuous week-column grid in BOTH orientations.
  int gridRight=0;std::set<int> columns;for(const auto& c:r.fills){gridRight=std::max(gridRight,c.x+c.w);columns.insert(c.x);}
  assert(columns.size()==14);
  assert(gridRight==width-20-2); // Full plot width after weekday band; 2px today-marker clearance.
  assert(r.fills.size()==96 && r.frames.size()==1 && r.pixels.empty());
  assert(r.texts.size()<=17); // One count, small month/weekday labels, no paragraphs/legend/range.
  for(const auto& t:r.texts){assert(t.x>=0 && t.y>=64 && t.x+r.getTextWidth(10,t.value.c_str())<=width);}
  for(size_t i=0;i<r.texts.size();++i)for(size_t j=i+1;j<r.texts.size();++j){
   const auto& a=r.texts[i];const auto& b=r.texts[j];
   assert(a.x+r.getTextWidth(10,a.value.c_str())<=b.x || b.x+r.getTextWidth(10,b.value.c_str())<=a.x || a.y+22<=b.y || b.y+22<=a.y);
  }
  for(const auto& c:r.fills){assert(c.x>=0 && c.y>=64 && c.x+c.w<=width && c.y+c.h<r.height-60 && c.w>0 && c.h>0);}
  for(const auto& c:r.frames){assert(c.x>=0 && c.y>=64 && c.x+c.w<=width && c.y+c.h<r.height-60);}
  assert(stats.readingHistoryBits==before);
  GlobalReadingStats empty;GfxRenderer e{width,r.height,{},{},{},{}};renderReadingHeatmapPage(e,&input,empty,true,{});
  assert(e.fills.empty() && e.pixels.size()==96 && e.frames.size()==1);
  assert(e.texts.size()<=17);bool honestEmpty=false;for(const auto& t:e.texts)if(t.value=="No history")honestEmpty=true;assert(honestEmpty);
  validClock=false;GfxRenderer invalid{width,r.height,{},{},{},{}};renderReadingHeatmapPage(invalid,&input,empty,true,{});
  assert(invalid.fills.empty() && invalid.frames.empty() && invalid.pixels.empty());
  assert(invalid.texts.size()==1 && invalid.texts[0].value=="Set clock");validClock=true;
  RecentReadingStats recent;recent.count=3;
  recent.books[0]={"/a.epub","Neutral Book A","cached-A",7200};
  recent.books[1]={"/b.epub",std::string(180,'L'),"missing",60};
  recent.books[2]={"/c.epub","Neutral Book C","corrupt",0};
  GfxRenderer cards{width,r.height,{},{},{},{}};placeholders.clear();opened.clear();
  renderReadingHeatmapPage(cards,&input,stats,true,recent);
  assert(placeholders.size()==2);bool lifetime=false,noTime=false;
  assert(placeholders[0].x==20 && placeholders[1].x==20);
  assert(placeholders[1].y>=placeholders[0].y+placeholders[0].height+8);
  int gridBottom=0;for(const auto& c:cards.fills)gridBottom=std::max(gridBottom,c.y+c.h);
  assert(placeholders[0].y>gridBottom);
  assert(placeholders[0].width+12+cards.getTextWidth(SMALL_FONT_ID,"71582788 min total")<=width-40);
  for(const auto& t:cards.texts)if(t.value=="120 min total" || t.value=="No recorded time")assert(t.x>20+placeholders[0].width);
  for(const auto& t:cards.texts){if(t.value=="120 min total")lifetime=true;if(t.value=="No recorded time")noTime=true;
   assert(t.x>=0 && t.x+cards.getTextWidth(SMALL_FONT_ID,t.value.c_str())<=width && t.y+22<cards.height-60);}
  assert(lifetime && noTime);
  for(const auto& c:cards.frames)assert(c.x>=0 && c.y>=64 && c.x+c.w<=width && c.y+c.h<cards.height-60);
  for(const auto& c:placeholders)assert(c.width>0 && c.height>0 && c.y+c.height<cards.height-60);
  validClock=false;GfxRenderer invalidCards{width,r.height,{},{},{},{}};renderReadingHeatmapPage(invalidCards,&input,stats,true,recent);
  bool invalidLifetime=false;for(const auto& t:invalidCards.texts)if(t.value=="120 min total")invalidLifetime=true;assert(invalidLifetime);validClock=true;
  for(size_t n=0;n<=3;++n){
   recent.count=n;recent.books[0].totalReadingSeconds=UINT32_MAX;
   recent.books[0].title="Über die Hügel – "+std::string(200,'X');
   for(size_t i=0;i<n;++i)recent.books[i].coverBmpPath="missing";
   GfxRenderer variant{width,r.height,{},{},{},{}};placeholders.clear();renderReadingHeatmapPage(variant,&input,stats,true,recent);
   assert(placeholders.size()==n);size_t totals=0;bool maximum=n==0;
   for(const auto& t:variant.texts){if(t.value=="71582788 min total")maximum=true;if(t.value.find(" total")!=std::string::npos || t.value=="No recorded time")++totals;
    assert(t.x+variant.getTextWidth(SMALL_FONT_ID,t.value.c_str())<=width-20 && t.y+22<variant.height-60);}
   assert(maximum && totals==n);
   for(size_t i=1;i<n;++i)assert(placeholders[i].x==20 && placeholders[i].y>=placeholders[i-1].y+placeholders[i-1].height+8);
  }
 }
 GlobalReadingStats tinyStats;MappedInputManager input;
 GfxRenderer tiny{160,240,{},{},{},{}};renderReadingHeatmapPage(tiny,&input,tinyStats,true,{});
 for(const auto& c:tiny.frames)assert(c.x>=0 && c.y>=0 && c.w>0 && c.h>0 && c.x+c.w<=tiny.width && c.y+c.h<tiny.height-60);
 for(const auto& c:tiny.pixels)assert(c.first>=0 && c.second>=0 && c.first<tiny.width && c.second<tiny.height-60);
 puts("PASS actual heatmap render: one grid portrait/landscape, 96 cells, bounded minimal labels, honest empty/invalid, read-only");
}
'''
with tempfile.TemporaryDirectory(prefix='heatmap-render-',dir=os.environ.get('TMPDIR','/root/.hermes/cache/scratch')) as work:
 d=Path(work);(d/'render.cpp').write_text(harness)
 # Stub only SDK clock/settings headers for the real calendar utilities.
 (d/'HalClock.h').write_text('#pragma once\n#include <cstdint>\nstruct Clock { bool getDateTime(uint16_t&,uint8_t&,uint8_t&,uint8_t&,uint8_t&){return false;} }; inline Clock halClock;\n')
 (d/'CrossPointSettings.h').write_text('#pragma once\nstruct Settings { int clockUtcOffsetQ=48; }; inline Settings SETTINGS;\n')
 # The probe supplies the clock function, so compile production utility under a private renamed clock symbol.
 flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-include','cstdio','-DgetCurrentLocalReadingStatsDateTime=unusedProductionClock','-I'+str(d),'-I'+str(R/'src/activities/reader'),'-c',str(R/'src/activities/reader/ReadingStatsUtils.cpp'),'-o',str(d/'utils.o')],check=True)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I'+str(R/'src/activities/reader'),str(d/'render.cpp'),str(d/'utils.o'),'-o',str(d/'render')],check=True)
 subprocess.run([str(d/'render')],check=True)
