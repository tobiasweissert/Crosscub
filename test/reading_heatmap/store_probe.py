#!/usr/bin/env python3
"""Cold Stats entry through actual RecentBooksStore and its JSON/binary collaborators.
Only HAL, EPUB/XTC metadata/cache and persistence write acknowledgements are doubled.
"""
from pathlib import Path
import argparse, os, subprocess, tempfile
R = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
json_root = Path(os.environ.get('READING_HEATMAP_ARDUINOJSON', '/root/.hermes/cache/scratch/clipping-release-sanitized/_deps/arduinojson-src/src'))
if not (json_root/'ArduinoJson.h').is_file():
 raise SystemExit('Set READING_HEATMAP_ARDUINOJSON to an existing ArduinoJson src directory; no download is performed')
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR', '/root/.hermes/cache/scratch')) as work:
 d = Path(work)
 # Sparse checkout: export only the exact repository collaborators, never SDK files.
 for name in ['PersistableStore.h', 'Serialization.h']:
  (d/name).write_bytes(subprocess.check_output(['git', 'show', 'HEAD:lib/Serialization/'+name], cwd=R))
 core = subprocess.check_output(['git', 'show', 'HEAD:lib/Serialization/PersistableStore.cpp'], cwd=R).decode()
 start = core.index('bool PersistableStoreBase::readDocFromFile(')
 end = core.index('\n}\n', start)+3
 (d/'persist.cpp').write_text('#include "PersistableStore.h"\n#include "HalStorage.h"\n'+core[start:end]+'''
bool PersistableStoreBase::writeDocToFileAtomically(const char*,const JsonDocument&){++Storage.mutations;return true;}
bool PersistableStoreBase::writeDocToFile(const char*,const JsonDocument&){++Storage.mutations;return true;}
''')
 (d/'Arduino.h').write_text('#pragma once\n#include <string>\nusing String=std::string;\n#define isEmpty empty\n')
 (d/'Epub.h').write_text('''#pragma once
#include <string>
inline unsigned epubConstructions=0;
struct Epub {enum class XLocationLoadMode{Skip};Epub(const std::string&,const char*){++epubConstructions;}
void load(bool,bool,XLocationLoadMode){}std::string getTitle(){return "";}std::string getAuthor(){return "";}std::string getThumbBmpPath(){return "";}
static std::string cachePathForFilePath(const std::string& p,const std::string&){return p+"-cache";}};
''')
 (d/'Xtc.h').write_text('''#pragma once
#include <string>
struct Xtc {std::string p;Xtc(std::string p,const char*):p(p){}bool load(){return false;}std::string getTitle(){return "";}std::string getAuthor(){return "";}std::string getThumbBmpPath(){return "";}std::string getCachePath(){return p+"-cache";}};
''')
 (d/'FsHelpers.h').write_text('''#pragma once
#include <string>
struct FsHelpers {static bool hasXtcExtension(const std::string& p){return p.ends_with(".xtc");}static bool hasEpubExtension(const std::string& p){return p.ends_with(".epub");}static bool hasTxtExtension(const std::string& p){return p.ends_with(".txt");}static bool hasMarkdownExtension(const std::string& p){return p.ends_with(".md");}};
''')
 (d/'test.cpp').write_text(r'''
#include <cassert>
#include <cstdio>
#include "HalStorage.h"
#include "RecentBooksStore.h"
#include "RecentReadingStats.h"
#include "BookReadingStats.h"
#include "Serialization.h"
#include "Epub.h"
// Lifetime decoding is qualified by codec_probe; this probe isolates the actual store lifecycle.
BookReadingStats BookReadingStats::load(const std::string&,LoadMode){return {};}
void put(const std::string& path,const std::string& text){HalFile f;assert(Storage.openFileForWrite("TEST",path,f));f.write(text.data(),text.size());f.close();}
int main(int argc,char** argv){
 assert(argc==3);int version=std::stoi(argv[1]);std::string fault=argv[2];
 const std::string bin="/.crosspoint/recent.bin";
 HalFile f;assert(Storage.openFileForWrite("TEST",bin,f));
 serialization::writePod(f,static_cast<uint8_t>(version));serialization::writePod(f,uint8_t(1));
 serialization::writeString(f,"/a.epub");
 if(version>=2){serialization::writeString(f,"Stored title");serialization::writeString(f,"Stored author");}
 if(version==3)serialization::writeString(f,"stored-cover");
 f.close();
 put("/a.epub","");
 put("/.crosspoint/recent.bin.bak","original-bin-backup");
 put("/.crosspoint/recent.json.tmp","unacknowledged-temp");
 if(fault=="json" || fault=="json-open" || fault=="json-recovery"){
  put("/.crosspoint/recent.json",R"({"books":[{"path":"/a.epub","title":"JSON title"}]})");
  if(fault=="json-open")Storage.failReadPath="/.crosspoint/recent.json";
  if(fault=="json-recovery")put("/.crosspoint/recent.json.bak",R"({"books":[{"path":"/a.epub","title":"Backup title"}]})");
 }
 if(fault=="binary-open")Storage.failReadPath=bin;
 if(fault=="truncated")put(bin,std::string(1,static_cast<char>(version)));
 if(fault=="bad-version")put(bin,std::string(1,static_cast<char>(99)));
 if(fault=="json-corrupt")put("/.crosspoint/recent.json","{broken");
 auto bytes=Storage.snapshot();unsigned writes=Storage.mutations;
 // No Home/getBooks boot initialization: this is the first store consumer.
 auto rows=RecentReadingStats::load();
 assert(Storage.mutations==writes && Storage.snapshot()==bytes);
 assert(epubConstructions==0);
 if(fault=="binary-open" || fault=="json-open" || fault=="truncated" || fault=="bad-version" || fault=="json-corrupt")assert(rows.count==0);
 else {
  assert(rows.count==1 && rows.books[0].path=="/a.epub");
  if(fault=="json")assert(rows.books[0].title=="JSON title");
  else if(fault=="json-recovery")assert(rows.books[0].title=="Backup title");
  else if(version>=2)assert(rows.books[0].title=="Stored title");
 }
 if(fault=="binary-open" || fault=="json-open")assert(Storage.readFaults>0);
 if(fault=="none"){
  // Snapshot must not mark the singleton loaded: ordinary Home still migrates.
  Storage.failReadPath.clear();RECENT_BOOKS.getBooks();
  assert(Storage.mutations>writes);
 }
 printf("PASS R2 actual cold store v%d/%s: zero writes/renames/EPUB constructors, unchanged bytes; normal migration retained\n",version,fault.c_str());
}
''')
 flags = ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []
 cmd = ['g++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-function',*flags,'-include','cstdio','-DARDUINOJSON_ENABLE_ARDUINO_STRING=0', '-I'+str(d),'-I'+str(R/'test/reading_heatmap/stubs'),'-I'+str(json_root),'-I'+str(R/'src'),'-I'+str(R/'src/activities/reader'),str(d/'test.cpp'),str(d/'persist.cpp'),str(R/'src/RecentBooksStore.cpp'),str(R/'src/activities/reader/RecentReadingStats.cpp'),'-o',str(d/'test')]
 subprocess.run(cmd,check=True)
 for version in [3,1,2]:
  for fault in ['none','binary-open','truncated','bad-version','json','json-open','json-corrupt','json-recovery']:
   subprocess.run([str(d/'test'),str(version),fault],check=True)
