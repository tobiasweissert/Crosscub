#!/usr/bin/env python3
"""Execute production Notes adapters/controller, never call a provider or use a key."""
import argparse, json, os, pathlib, re, shutil, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--json-include',type=pathlib.Path,default=os.environ.get('ARDUINOJSON_INCLUDE'));a=p.parse_args()
if a.json_include is None:
 a.json_include=next((p.parent for p in (ROOT/'.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h')),None)
if a.json_include is None or not (a.json_include/'ArduinoJson.h').is_file():
 raise SystemExit('Set ARDUINOJSON_INCLUDE to the installed pinned ArduinoJson/src; no downloads performed')
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'}
with tempfile.TemporaryDirectory(prefix='notes-',dir=os.environ.get('TMPDIR')) as tmp:
 out=pathlib.Path(tmp)
 sanitize=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []
 common=['-std=c++17','-Wall','-Wextra','-Werror','-g',*sanitize]
 # Extract exact production layout methods, with a labelled deterministic metric
 # seam. This is not a parallel implementation or a real-font renderer claim.
 source=(ROOT/'src/activities/util/KeyboardEntryActivity.cpp').read_text()
 def block(source: str,start):
  brace=source.index('{',start);depth=1;end=brace+1
  while depth:
   depth+=(source[end]=='{')-(source[end]=='}');end+=1
  return source[start:end]
 methods=[]
 for name in ('utf8Prev','utf8Next','displayTextForCurrentState','measureRange','rangeIsRtl','lineBreakEnd','inputFieldTouchTargetFromPoint','inputStartY','inputViewportStart','keyboardRect','snapshotTouch','snapshotVertical'):
  match=re.search(r'^[a-zA-Z0-9_:<> ]+ KeyboardEntryActivity::'+name+'[(]',source,re.M)
  assert match,name
  methods.append(block(source,match.start()))
 (out/'layout-production.inc').write_text('\n'.join(methods))
 (out/'selection-production.inc').write_text(block(source,source.index('if (snapshotSelection != std::string::npos && snapshotSelection != cursorPos)')))
 subprocess.run(['c++',*common,'-DFREEINK_DEVICE_STICKY=1','-I'+str(out),str(ROOT/'test/notes_realtime/layout.cpp'),'-o',str(out/'layout')],check=True)
 subprocess.run([str(out/'layout')],check=True,env=env,timeout=15)
 luaObjects=[]
 for src in sorted((ROOT/'lib/Lua/src').glob('*.c')):
  obj=out/(src.stem+'.o');subprocess.run(['cc','-g',*sanitize,'-I'+str(ROOT/'lib/Lua/src'),'-c',str(src),'-o',str(obj)],check=True);luaObjects.append(str(obj))
 for name in ('wire','audio','protocol','transport','microphone','session','binding'):
  source=ROOT/'test/notes_realtime'/f'{name}.cpp'
  includes=['-I'+str(ROOT/'src')];extra=[]
  if name=='transport':
   s=(ROOT/'src/util/PluginHttp.cpp').read_text();helpers=s[s.index('namespace {'):s.index('namespace pluginhttp {')]
   source=out/'transport.cpp';source.write_text('#include "host.h"\n#include "PluginHttp.h"\n'+helpers+'\nnamespace pluginhttp {std::string escapeUrl(const std::string& s){return s;}}\n#include "'+str(ROOT/'test/notes_realtime/transport.cpp')+'"\n')
   includes=['-I'+str(ROOT/'test/crosspoint_migration/plugin-transport'),'-I'+str(ROOT/'src/util'),*includes];extra=['-lcrypto']
  else:
   includes=['-I'+str(a.json_include),'-I'+str(ROOT/'test/notes_realtime/stubs'),*includes]
  if name in ('microphone','session','binding'):extra+=[str(ROOT/'src/hal/NotesMicrophone.cpp'),'-pthread']
  if name in ('session','binding'):extra+=[str(ROOT/'src/network/NotesDictation.cpp')]
  if name=='binding':includes+=['-I'+str(ROOT/'lib/Lua/src')];extra+=[*luaObjects,'-lm','-ldl']
  subprocess.run(['c++',*common,*includes,str(source),*extra,'-o',str(out/name)],check=True)
  subprocess.run([str(out/name)],check=True,env=env,timeout=15)
 runner=out/'lua'
 # Exercise the real activity adapter's lifecycle dispatch, including ordinary Notes runners.
 flags=['-O1','-g',*sanitize,'-I'+str(ROOT/'test/browser/activity_stubs'),'-I'+str(ROOT/'lib/Memory'),'-I'+str(ROOT/'lib/miniz/src'),'-I'+str(ROOT/'lib/LuaBrowser'),'-I'+str(ROOT/'lib/Lua/src'),'-I'+str(ROOT/'lib/LuaHttp'),'-I'+str(ROOT/'src/activities/apps/lua'),'-I'+str(a.json_include),'-I'+str(ROOT/'src')]
 subprocess.run(['cc',*flags,'-c',str(ROOT/'lib/miniz/src/miniz_impl.c'),'-o',str(out/'miniz.o')],check=True)
 subprocess.run(['c++','-std=c++20',*flags,str(ROOT/'test/browser/activity.cpp'),*[str(ROOT/p) for p in ['lib/AirDrop/browser/BrowserLaunch.cpp','lib/AirDrop/core/Offer.cpp','lib/LuaHttp/LuaHttp.cpp','lib/LuaHttp/HttpCore.cpp','lib/LuaHttp/HttpGzip.cpp','lib/hal/HalLuaResources.cpp']],str(out/'miniz.o'),*luaObjects,'-lm','-ldl','-pthread','-o',str(out/'activity')],check=True)
 subprocess.run([str(out/'activity')],cwd=ROOT,env=env,check=True,timeout=60)
 subprocess.run(['cc','-g',*sanitize,'-I'+str(ROOT/'lib/Lua/src'),str(ROOT/'test/crossplay_ports/runner.c'),*luaObjects,'-lm','-o',str(runner)],check=True)
 scripts=[(name,ROOT/'test/notes_realtime'/f'{name}.lua') for name in ('editor','integration','settings')]
 scripts += [(name,ROOT/'test/crossplay_ports'/file) for name,file in (('core','notes.lua'),('journal','notes_integration.lua'),('capacity','notes_capacity.lua'))]
 for script,path in scripts:
  for w,h in ((480,800),(800,480)):
   fixture=out/f'{script}-{w}';shutil.copytree(ROOT/'apps/cp-notes',fixture)
   result=subprocess.run([str(runner),str(path),'2097152',str(w),str(h)],cwd=fixture,env=env,text=True,capture_output=True,timeout=15)
   print(result.stdout,result.stderr,end='',flush=True);result.check_returncode()
   metrics=json.loads(result.stdout.strip().splitlines()[-1]);assert metrics['after_close']==0 and metrics['ok']
print('PASS Notes real production host adapters + Lua controller/app, '+('ASan/UBSan' if a.sanitize else 'normal')+'; no target/hardware/provider qualification',flush=True)
