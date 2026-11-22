#!/usr/bin/env python3
"""Real BookReadingStats codec + recent loader, in-memory storage/HAL boundaries."""
from pathlib import Path
import argparse, os, subprocess, tempfile
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR','/root/.hermes/cache/scratch')) as work:
 d=Path(work)
 (d/'RecentBooksStore.h').write_text('''#pragma once
#include <string>
#include <vector>
struct RecentBook {std::string path,title,author,coverBmpPath;};
struct Store {std::vector<RecentBook> books;const auto& readOnlySnapshot()const{return books;}};inline Store RECENT_BOOKS;
''')
 (d/'Epub.h').write_text('''#pragma once
#include <string>
struct Epub {static std::string cachePathForFilePath(const std::string& p,const std::string&){return p+"-cache";}};
''')
 (d/'Xtc.h').write_text('''#pragma once
#include <string>
struct Xtc {std::string p;Xtc(std::string p,const char*):p(p){}std::string getCachePath(){return p+"-cache";}};
''')
 (d/'FsHelpers.h').write_text('''#pragma once
#include <string>
struct FsHelpers {static bool hasXtcExtension(const std::string& p){return p.ends_with(".xtc");}};
''')
 (d/'HalClock.h').write_text('#pragma once\n#include <cstdint>\nstruct Clock {bool getDateTime(uint16_t&,uint8_t&,uint8_t&,uint8_t&,uint8_t&){return false;}};inline Clock halClock;\n')
 (d/'CrossPointSettings.h').write_text('#pragma once\nstruct Settings {int clockUtcOffsetQ=48;};inline Settings SETTINGS;\n')
 cpp=(R/'src/activities/reader/BookStatsActivity.cpp').read_text()
 start=cpp.index('void BookStatsActivity::onEnter()'); end=cpp.index('\n}\n',start)+3
 entry=cpp[start:end].replace('BookStatsActivity::','')
 constructors=cpp[cpp.index('BookStatsActivity::BookStatsActivity('):cpp.index('void BookStatsActivity::refreshAllDevicesStats')].replace('BookStatsActivity::BookStatsActivity','Entry')
 source=r'''
#include <cassert>
#include <cstdio>
#include "HalStorage.h"
#include "BookReadingStats.h"
#include "RecentReadingStats.h"
#include "RecentBooksStore.h"
void put(const std::string& path,const std::vector<uint8_t>& bytes){HalFile f;assert(Storage.openFileForWrite("TEST",path,f));assert(f.write(bytes.data(),bytes.size())==bytes.size());f.close();}
struct MappedInputManager {enum class Button{Back,Confirm,Power};bool isPressed(Button){return false;}};
struct GfxRenderer {enum class Orientation{Portrait};Orientation getOrientation(){return Orientation::Portrait;}void setOrientation(Orientation){}};
struct GlobalReadingStats {};
struct Activity {
 GfxRenderer& renderer;MappedInputManager& mappedInput;
 Activity(const char*,GfxRenderer& r,MappedInputManager& m):renderer(r),mappedInput(m){}
 void onEnter(){}
};
struct Entry : Activity {
 std::string bookTitle,bookCachePath;BookReadingStats stats;GlobalReadingStats globalStats,allDevicesStats;
 bool showAllDevicesStats=false,returnToHomeOnExit=false;float progressPercent;
 bool hasEstimatedTimeLeft;uint32_t estimatedTimeLeftSeconds;
 RecentReadingStats recentReadingStats;GfxRenderer::Orientation previousOrientation;
 bool ignoreInitialBackRelease=false,ignoreInitialConfirmRelease=false,ignoreInitialPowerRelease=false;
 void requestUpdate(){}
 /*ACTUAL_CONSTRUCTORS*/
 /*ACTUAL_ENTRY*/
};
int main(){
 for(auto path:{"/a.epub","/b.xtc","/c.md"}){put(path,{});RECENT_BOOKS.books.push_back({path,path,"",""});}
 BookReadingStats a;a.totalReadingSeconds=123456789;a.sessionCount=4;assert(a.save("/a.epub-cache"));
 put("/b.xtc-cache/stats_v5.bin",{5,0,0,255}); // truncated, never publish invented duration
 unsigned before=Storage.mutations;auto rows=RecentReadingStats::load();
 assert(rows.count==3 && rows.books[0].totalReadingSeconds==123456789);
 assert(rows.books[1].totalReadingSeconds==0 && rows.books[2].totalReadingSeconds==0 && Storage.mutations==before);
 std::vector<uint8_t> legacy(11);legacy[0]=1;legacy[3]=60;put("/c.md-cache/stats.bin",legacy);
 before=Storage.mutations;rows=RecentReadingStats::load();assert(rows.books[2].totalReadingSeconds==60 && Storage.mutations==before);
 // Actual Activity entry must replace only the matching cache identity, including lower reset previews.
 for(uint32_t persisted:{0u,100u})for(uint32_t preview:{0u,20u,1000u,UINT32_MAX}){
  BookReadingStats disk;disk.totalReadingSeconds=persisted;assert(disk.save("/a.epub-cache"));
  for(const auto& cache:{std::string("/a.epub-cache"),std::string("/different-cache"),std::string()}){
   auto original=RECENT_BOOKS.books;unsigned writes=Storage.mutations;
   for(bool aggregate:{false,true}){
   GfxRenderer renderer;MappedInputManager input;GlobalReadingStats global;
   BookReadingStats live;live.totalReadingSeconds=preview;live.totalPagesTurned=123;live.sessionCount=7;
   Entry activity=aggregate?Entry(renderer,input,"Live title",cache,live,0.375f,true,321,global,global,false)
                           :Entry(renderer,input,"Live title",cache,live,0.375f,true,321,global,false);
   activity.onEnter();
   assert(activity.progressPercent==0.375f && activity.stats.totalPagesTurned==123 && activity.stats.sessionCount==7);
   assert(activity.estimatedTimeLeftSeconds==321 && activity.showAllDevicesStats==aggregate);
   assert(activity.recentReadingStats.books[0].totalReadingSeconds==(cache=="/a.epub-cache"?preview:persisted));
   assert(activity.recentReadingStats.books[1].totalReadingSeconds==0 && activity.recentReadingStats.books[2].totalReadingSeconds==60);
   assert(activity.stats.totalReadingSeconds==preview && Storage.mutations==writes);
   for(size_t i=0;i<original.size();++i)assert(original[i].path==RECENT_BOOKS.books[i].path);
   }
  }
 }
 puts("PASS R1 actual Activity entry: persisted zero/nonzero, live higher/lower/reset/saturated, other/empty identity, no addition/reorder/save");
 // Complete recovery artifacts must be preserved rather than renamed by a view.
 assert(Storage.rename("/a.epub-cache/stats_v5.bin","/a.epub-cache/stats_v5.bin.tmp"));
 before=Storage.mutations;rows=RecentReadingStats::load();assert(rows.books[0].totalReadingSeconds==0);
 assert(Storage.mutations==before && Storage.exists("/a.epub-cache/stats_v5.bin.tmp"));
 assert(!Storage.exists("/a.epub-cache/stats_v5.bin"));
 // exists=true is not open success: Stats must never attempt recovery mutation.
 put("/a.epub-cache/stats_v5.bin",std::vector<uint8_t>(73,0));
 put("/a.epub-cache/stats_v5.bin.bak",std::vector<uint8_t>(73,5));
 Storage.failReadPath="/a.epub-cache/stats_v5.bin";
 auto recoveryBytes=Storage.snapshot();before=Storage.mutations;
 rows=RecentReadingStats::load();
 assert(Storage.readFaults>0);
 assert(Storage.mutations==before && Storage.snapshot()==recoveryBytes);
 assert(rows.books[0].totalReadingSeconds==0);
 Storage.failReadPath.clear();
 // The default reader mode still owns actual recovery/rename.
 put("/ordinary-cache/stats_v5.bin.tmp",recoveryBytes.at("/a.epub-cache/stats_v5.bin.tmp"));
 before=Storage.mutations;
 auto recovered=BookReadingStats::load("/ordinary-cache");
 assert(recovered.totalReadingSeconds==100 && Storage.mutations>before);
 assert(Storage.exists("/ordinary-cache/stats_v5.bin") && !Storage.exists("/ordinary-cache/stats_v5.bin.tmp"));
 // Bad version is also the actual codec's empty stats, never random LE seconds.
 std::vector<uint8_t> bad(73,255);put("/b.xtc-cache/stats_v5.bin",bad);
 before=Storage.mutations;rows=RecentReadingStats::load();assert(rows.books[1].totalReadingSeconds==0 && Storage.mutations==before);
 puts("PASS real BookReadingStats codec + recent loader: lifetime uint32, truncated/invalid/missing stats, legacy v1, recovery preserved, zero storage writes");
}
'''
 (d/'test.cpp').write_text(source.replace('/*ACTUAL_ENTRY*/',entry).replace('/*ACTUAL_CONSTRUCTORS*/',constructors))
 flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []
 cmd=['g++','-std=c++20','-Wall','-Wextra','-Werror',*flags,'-include','cstdio','-I'+str(d),'-I'+str(R/'test/reading_heatmap/stubs'),'-I'+str(R/'src/activities/reader'),str(d/'test.cpp'),*[str(R/'src/activities/reader'/n) for n in ['RecentReadingStats.cpp','BookReadingStats.cpp','ReadingStatsUtils.cpp']],'-o',str(d/'test')]
 subprocess.run(cmd,check=True);subprocess.run([str(d/'test')],check=True)
