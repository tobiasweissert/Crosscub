#!/usr/bin/env python3
"""Production recent-book selection/load body; SDK/store/cache boundaries are doubles."""
from pathlib import Path
import argparse, os, subprocess, tempfile
R = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(); p.add_argument('--sanitize', action='store_true'); a = p.parse_args()
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR', '/root/.hermes/cache/scratch')) as work:
 d = Path(work)
 (d/'RecentBooksStore.h').write_text('''#pragma once
#include <string>
#include <vector>
struct RecentBook { std::string path,title,author,coverBmpPath; };
struct Store { std::vector<RecentBook> books; const auto& readOnlySnapshot()const{return books;} }; inline Store RECENT_BOOKS;
''')
 (d/'Epub.h').write_text('''#pragma once
#include <string>
struct Epub { static std::string cachePathForFilePath(const std::string& p,const std::string& r){return r+"/epub:"+p;} };
''')
 (d/'Xtc.h').write_text('''#pragma once
#include <string>
struct Xtc {std::string p; Xtc(std::string p,const char*):p(p){}std::string getCachePath(){return "/.crosspoint/xtc:"+p;} };
''')
 (d/'FsHelpers.h').write_text('''#pragma once
#include <string>
struct FsHelpers {static bool hasXtcExtension(const std::string& p){return p.ends_with(".xtc");}};
''')
 (d/'HalStorage.h').write_text('''#pragma once
#include <set>
#include <vector>
#include <string>
struct StorageDouble {std::set<std::string> files; bool exists(const char* p){return files.count(p);} std::vector<std::string> listFiles(const char*,int){return {"page_1.bmp","thumb_adaptive_200x300.bmp","stats_v5.bin"};}};inline StorageDouble Storage;
''')
 # Quoted BookReadingStats include resolves in production source directory; test its interface at link seam.
 source = r'''
#include <cassert>
#include <cstdio>
#include <map>
#include "RecentBooksStore.h"
#include "HalStorage.h"
#include "RecentReadingStats.h"
#include "BookReadingStats.h"
std::map<std::string,uint32_t> durations; std::vector<std::string> loads;
BookReadingStats BookReadingStats::load(const std::string& p, LoadMode mode){assert(mode==LoadMode::ReadOnly);loads.push_back(p); BookReadingStats s;s.totalReadingSeconds=durations[p];return s;}
int main(){
 RECENT_BOOKS.books={{"/a.epub","A","","a-cover"},{"/a.epub","wrong duplicate","","wrong-cover"},{"/deleted.epub","deleted","","deleted-cover"},{"/b.xtc","B","","b-cover"},{"/c.txt","C","",""},{"/d.epub","D","","d-cover"}};
 for(auto p:{"/a.epub","/b.xtc","/c.txt","/d.epub"})Storage.files.insert(p);
 durations["/.crosspoint/epub:/a.epub"]=12345; durations["/.crosspoint/xtc:/b.xtc"]=987;
 auto snapshot=RECENT_BOOKS.books;auto files=Storage.files;
 auto rows=RecentReadingStats::load();
 assert(rows.count==3);assert(rows.books[0].path=="/a.epub" && rows.books[0].coverBmpPath=="a-cover" && rows.books[0].totalReadingSeconds==12345);
 assert(rows.books[1].path=="/b.xtc" && rows.books[1].totalReadingSeconds==987);
 assert(rows.books[2].path=="/c.txt" && rows.books[2].totalReadingSeconds==0);
 assert(loads.size()==3 && loads[2]=="/.crosspoint/epub:/c.txt");
 assert(Storage.files==files && RECENT_BOOKS.books.size()==snapshot.size() && RECENT_BOOKS.books[1].title==snapshot[1].title);
 puts("PASS recent production loader: stable path identity, dedupe, deleted skip, cap three, EPUB/XTC selection, absent TXT record, lifetime seconds, no mutation");
 // Recovery admission belongs to the real codec probe, not this link seam.
 loads.clear(); Storage.files.insert("/.crosspoint/epub:/a.epub/stats_v5.bin.tmp");
 rows=RecentReadingStats::load();assert(rows.books[0].totalReadingSeconds==12345 && loads.size()==3);
 Storage.files.insert("/.crosspoint/epub:/a.epub/stats_v5.bin");
 rows=RecentReadingStats::load();assert(rows.books[0].totalReadingSeconds==12345);
 RECENT_BOOKS.books={{"","empty","",""},{"/a.epub","","",""}};
 rows=RecentReadingStats::load();assert(rows.count==1 && rows.books[0].title=="a.epub");
 RECENT_BOOKS.books={{"/a.epub","A","","/.crosspoint/epub:/a.epub/thumb_[WIDTH]x[HEIGHT].bmp"}};
 rows=RecentReadingStats::load();assert(rows.books[0].coverBmpPath=="/.crosspoint/epub:/a.epub/thumb_adaptive_200x300.bmp");
 RECENT_BOOKS.books.clear();rows=RecentReadingStats::load();assert(rows.count==0);
 puts("PASS fewer/zero/empty-path/title books; read-only recovery admission; missing duration remains zero, never Boolean/session-derived");
}
'''
 (d/'test.cpp').write_text(source)
 flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []
 subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',*flags,'-I'+str(d),'-I'+str(R/'src/activities/reader'),str(d/'test.cpp'),str(R/'src/activities/reader/RecentReadingStats.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
