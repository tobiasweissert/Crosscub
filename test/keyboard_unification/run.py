#!/usr/bin/env python3
"""Bounded genuine SDK/Gfx/production-Lua keyboard tracer; no firmware build.
Requires the explicitly selected, hash-verified Stats native cache. Network is
removed by a Linux network namespace before running either renderer. Lifecycle
stack/mutex seams are labelled in runner.cpp; not full application navigation.
"""
import argparse, fcntl, hashlib, json, os, re, shlex, shutil, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--filter');p.add_argument('--native-only',action='store_true');p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');p.add_argument('--phase',default='green');a=p.parse_args();O=a.output.resolve();O.mkdir(parents=True,exist_ok=True)
B=Path(os.environ.get('KEYBOARD_CACHE_ROOT','/root/.hermes/task-state/crosscub-owner-next-release'))
N=B/'native-unblock';C=N/'continuation-run1';S=B/'stats/icon-orientation-fix-round1'
L=Path(os.environ.get('KEYBOARD_LUA_ROOT','/root/github/crosscub-sticky-no-lexend/lib/Lua/src'))
app_fixtures=os.environ.get('KEYBOARD_APP_FIXTURES')
if not app_fixtures or not Path(app_fixtures).is_dir():
 p.error('set KEYBOARD_APP_FIXTURES to the read-only keyboard donor apps directory')
A=Path(app_fixtures).resolve()
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def dump(n,d):(O/n).write_text(json.dumps(d,indent=2)+'\n')
rows=[]
def run(name,cmd,**kw):
 assert shutil.disk_usage('/').free>=128*1024**2,'Root safety margin'
 with (O/(name+'.log')).open('w') as f:rc=subprocess.run(cmd,cwd=R,stdout=f,stderr=subprocess.STDOUT,**kw).returncode
 rows.append(dict(name=name,command=cmd,exit=rc,log=str(O/(name+'.log'))));dump('commands.json',rows);print(name,rc,flush=True)
 if rc:print((O/(name+'.log')).read_text()[-5000:])
 return rc

def function(source,token):
 start=source.index(token);brace=source.index('{',start);level=1;i=brace+1
 # These callbacks contain no brace-bearing string literals.
 while level:
  if source[i]=='{':level+=1
  elif source[i]=='}':level-=1
  i+=1
 return source[start:i]

with open('/root/.hermes/cache/scratch/crosscub-review-probes.lock','a') as lock:
 fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 from closure_sources import generate
 generate(R,O)
 inputs={}
 for p in [R/'test/crosscub/run_all.py',C/'wolf-config.json',C/'inputmanifest.json',O/'closure-extractions.json']:
  inputs[str(p)]=sha(p)
 for row in json.loads((O/'closure-extractions.json').read_text()):
  p=R/row['source'];inputs[str(p)]=sha(p)
 for app in ['flashcards','habits','pomodoro','weather']:
  for p in (A/app).glob('*'):
   if p.is_file():inputs[str(p)]=sha(p)
  editor=(A/app/'editor.lua').read_text().replace("local K=dofile('keyboard.lua')\n",'').replace('K.button(U,','U.button(').replace(' if K.editor(U) then return end\n','')
  assert hashlib.sha256(editor.encode()).hexdigest()=='0aac72489160dd679ffc0503ec0b5ca4350908532137649d916cff3e22b9bd56'
  assert (A/app/'keyboard.lua').read_bytes()==(A/'flashcards/keyboard.lua').read_bytes()
 for app in ['browser','wordle','sudoku','cp-calculator']:
  for p in (A/app).rglob('*'):
   if p.is_file():inputs[str(p)]=sha(p)
 for p in (R/'test/keyboard_unification').glob('*'):
  if p.is_file():inputs[str(p)]=sha(p)
 source=(R/'src/activities/apps/lua/LuaRunner.cpp').read_text()
 adapter='#include <HttpManifest.h>\n#include "Memory.h"\n#include "Logging.h"\nusing namespace ink;\nstruct RunnerAdapter { GfxRenderer& renderer_; std::string appDir_; std::string appId_; };\nstatic RunnerAdapter active{renderer,"","fixture"};\nstatic RunnerAdapter* s_activeRunner=&active;\n'
 adapter+=source[source.index('static inline int getInt'):source.index('int LuaRunner::l_dofile')]
 funcs=['l_clear','l_text','l_textWidth','l_wrappedText','l_lineHeight','l_rect','l_rectDither','l_roundedRect','l_drawSprite','l_line','l_header','l_getMetrics']
 for f in funcs:adapter+='\n'+function(source,'int LuaRunner::'+f+'(').replace('LuaRunner::','')+'\n'
 adapter+='\nstatic int device(lua_State* L) {lua_pushliteral(L,"sticky");return 1;}\nstatic int bounds(lua_State* L) {lua_pushinteger(L,renderer.getScreenWidth());lua_pushinteger(L,renderer.getScreenHeight());return 2;}\nstatic int noop(lua_State*){return 0;}\n'
 adapter+='static void registerAdapter(lua_State* L){lua_newtable(L);\n'
 for name,fn in [('bitmap','l_drawSprite'),('rect_dither','l_rectDither'),('rounded_rect','l_roundedRect'),('line','l_line'),('header','l_header'),('clear','l_clear'),('text','l_text'),('text_width','l_textWidth'),('wrapped_text','l_wrappedText'),('line_height','l_lineHeight'),('rect','l_rect'),('get_device','device'),('get_bounds','bounds'),('get_metrics','l_getMetrics'),('button_hints','noop'),('request_update','noop')]:adapter+=f'lua_pushcfunction(L,{fn});lua_setfield(L,-2,"{name}");\n'
 header=R/'src/activities/apps/lua/LuaKeyboardPaint.h'
 if header.exists():adapter+='ink::keyboard::registerPaint(L,renderer);\n'
 adapter+='lua_setglobal(L,"smudge");}\n'
 if header.exists():adapter='#include "activities/apps/lua/LuaKeyboardPaint.h"\n'+adapter
 (O/'lua_adapter.inc').write_text(adapter)
 # Qualified Lua source is byte-identical to this checkout, not a system Lua.
 lua_names=subprocess.check_output(['git','ls-files','lib/Lua/src'],cwd=R,text=True).splitlines()
 for name in lua_names:
  q=L/Path(name).name
  assert q.read_bytes()==subprocess.check_output(['git','show','HEAD:'+name],cwd=R),name
  inputs[str(q)]=sha(q)
 flags=json.loads((B/'stats/native-commands.json').read_text())[0];flags=flags[1:flags.index('-c')]
 flags=['-DLUA_USE_POSIX','-I'+str(O),'-I'+str(C/'hal'),'-I'+str(N/'hal'),'-I'+str(N/'i18n'),'-I'+str(R/'lib/GfxRenderer'),'-I'+str(L),*flags,'-ffunction-sections','-fdata-sections','-fno-access-control']
 flags[0:0]=['-I'+str(p) for p in sorted((R/'lib').iterdir()) if p.is_dir()]
 flags+=json.loads((C/'wolf-config.json').read_text())['flags']+['-DFREEINK_CONTENT_WOLFSSL=1']
 if a.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
 def compile(src,name,c=False):
  obj=O/(name+'.o');cmd=['gcc' if c else 'g++',*([f for f in flags if not f.startswith('-std=')] if c else flags),*(['-std=gnu11'] if c else []),'-MMD','-MF',str(O/(name+'.d')),'-c',str(src),'-o',str(obj)]
  if run('compile-'+name,cmd):raise SystemExit(2)
  for dep in shlex.split((O/(name+'.d')).read_text().replace('\\\n',' ').split(':',1)[1]):
   if Path(dep).is_file():inputs[dep]=sha(dep)
  return str(obj)
 compile(R/'src/activities/apps/lua/LuaRunner.cpp','LuaRunner-compile-only')
 compile(R/'src/activities/reader/EpubReaderTouchMenuActivity.cpp','Drawer-full-TU-compile-only')
 objects=[]
 for q in sorted(L.glob('*.c')):
  dst=O/('lua-'+q.stem+'.o')
  # Reuse only this task's hash-bound identical compiler input/flags.
  key=hashlib.sha256((sha(q)+json.dumps(flags)).encode()).hexdigest()
  stamp=dst.with_suffix('.key')
  if not(dst.exists() and stamp.exists() and stamp.read_text()==key):
   compile(q,'lua-'+q.stem,True);stamp.write_text(key)
  objects.append(str(dst))
 objects += [compile(R/path,Path(path).stem) for path in ['src/activities/util/KeyboardEntryActivity.cpp','src/activities/util/KeyboardLayoutSet.cpp','src/util/ButtonNavigator.cpp','src/SettingsList.cpp','test/keyboard_unification/runner.cpp','src/activities/reader/EpubReaderPercentSelectionActivity.cpp','src/activities/settings/FrontlightTimePickerActivity.cpp','src/activities/settings/KeyboardLayoutsActivity.cpp']]
 credential=Path('/root/github/crosscub-crosspoint-rebase/lib/KOReaderSync/KOReaderCredentialStore.cpp')
 assert credential.read_bytes()==subprocess.check_output(['git','show','HEAD:lib/KOReaderSync/KOReaderCredentialStore.cpp'],cwd=R)
 objects.append(compile(credential,'KOReaderCredentialStore'))
 httpcore=Path('/root/github/crosscub-crosspoint-rebase/lib/LuaHttp/HttpCore.cpp')
 assert httpcore.read_bytes()==subprocess.check_output(['git','show','HEAD:lib/LuaHttp/HttpCore.cpp'],cwd=R)
 objects.append(compile(httpcore,'HttpCore'))
 httpheader=Path('/root/github/crosscub-crosspoint-rebase/lib/HttpHeader/HttpHeader.cpp')
 assert httpheader.read_bytes()==subprocess.check_output(['git','show','HEAD:lib/HttpHeader/HttpHeader.cpp'],cwd=R)
 objects.append(compile(httpheader,'HttpHeader'))
 objects.append(compile(Path('/root/github/crosscub-crosspoint-rebase/freeink-sdk/libs/book/ContentProtection/src/LcpLicense.cpp'),'LcpLicense'))
 crypto_manifest=json.loads((C/'inputmanifest.json').read_text())
 for row in crypto_manifest['objects']:
  obj=row['object'];name=Path(obj).name
  if not(name.startswith('wolf-') or name in ['new-WolfsslCrypto.o','new-native_entropy_linux.o']):continue
  assert sha(obj)==row['sha256'],obj
  for dep in row['source_dependencies']:
   assert sha(dep)==crypto_manifest['dependencies'][dep]['sha256'],dep
   inputs[dep]=sha(dep)
  inputs[obj]=sha(obj);objects.append(obj)
 link=json.loads((S/'native-build-results.json').read_text())[1]['command']
 # Verify all cached objects and their dependency preimages against the Stats
 # freeze that produced the successful binary; unknown reuse is rejected.
 manifest=json.loads((S/'source-inputmanifest.json').read_text())
 for obj in [x for x in link if x.endswith('.o') and not x.endswith('/render_main.o')]:
  assert sha(obj)==manifest[obj],obj
  dp=Path(obj).with_suffix('.d')
  if dp.exists():
   for dep in shlex.split(dp.read_text().replace('\\\n',' ').split(':',1)[1]):
    if Path(dep).is_file():
     assert dep in manifest,('unbound cached dependency',dep)
     assert sha(dep)==manifest[dep],dep
     inputs[dep]=sha(dep)
  inputs[obj]=sha(obj);objects.append(obj)
 objects=list(dict.fromkeys(objects))
 exe=O/'keyboard-sdl';cmd=['g++','-Wl,--gc-sections','-o',str(exe),*objects,'-lSDL2','-lcrypto','-lpthread']
 if a.sanitize:cmd+=['-fsanitize=undefined']
 if run('link',cmd):raise SystemExit(2)
 inputs[str(exe)]=sha(exe);dump('bindings.json',inputs)
 # No application HTTP providers are linked or loaded. unshare denies egress,
 # and the only Lua files entered are the editor/ui and package-local helpers.
 (O/'sd').mkdir(exist_ok=True)
 captures=[]
 from PIL import Image
 for mode,app in [('caller-'+id,'flashcards') for id in ['native.opds.lcp-passphrase','native.wifi.password','native.wifi.hidden-ssid','native.frontlight.start-hour','native.frontlight.start-minute','native.frontlight.end-hour','native.frontlight.end-minute','native.catalog.opds-search','native.catalog.plugin-search','native.library.search','native.file.rename','native.bookmark.rename','native.settings.device-name','native.opds.download-folder','native.koreader.username','native.koreader.password','native.koreader.server-url','native.koreader.header1','native.koreader.header2','native.opds.server-name','native.opds.server-url','native.opds.username','native.opds.password','native.opds.header1','native.opds.header2']]+[('drawer-font-'+pane+'-'+scale,'flashcards') for pane in ['percent','page'] for scale in ['small','large']]+[('snapshot-invalid','flashcards'),('snapshot-oom','flashcards'),('drawer-percent','flashcards'),('drawer-page','flashcards')]+[('layout-settings','flashcards'),('native','flashcards'),('native-long','flashcards'),('native-rtl','flashcards')]+[('numeric-percent','flashcards'),('numeric-page','flashcards'),('numeric-hour','flashcards'),('numeric-minute','flashcards')]+[(f'native-layout-{language}-{layer}','flashcards')for language in range(10)for layer in range(3)]+([] if a.native_only else [(m,p) for p in ['flashcards','habits','pomodoro','weather'] for m in ['lua','fallback','nonsticky']]+[('lua','browser'),('pad-wordle-enhanced','wordle'),('pad-wordle-compact','wordle'),('pad-sudoku','sudoku'),('pad-calculator-original','cp-calculator'),('pad-calculator-compact','cp-calculator')]):
  for orient in ([1] if mode.startswith('native-layout-') else [0,1,2,3]):
   name=a.phase+'-'+mode+'-'+app+'-'+str(orient);bmp=O/(name+'.bmp')
   if a.filter and a.filter not in name: continue
   env=dict(os.environ,CROSSPOINT_SIM_INPUT_SCRIPT='100:UP:700' if mode in ['native-long','native-rtl'] else '',KEYBOARD_CAPTURE_PREFIX=str(O/name),SDL_VIDEODRIVER='dummy',SDL_RENDER_DRIVER='software',CROSSPOINT_SIM_SD=str(O/'sd'),CROSSPOINT_SIM_NVS=str(O/'nvs'),CROSSPOINT_SIM_SCREENSHOTS='400:'+str(bmp))
   rc=run(name,['unshare','-n',str(exe),mode,str(A/app),str(orient),str(R/'test/keyboard_unification'/('pad_behavior.lua' if mode.startswith('pad-') else 'browser_behavior.lua' if app=='browser' else 'behavior.lua'))],env=env,timeout=20)
   if bmp.exists():
    img=Image.open(bmp);png=bmp.with_suffix('.png');img.save(png);bmp.unlink()
    if mode=='nonsticky':
     baseline=json.loads((R/'test/keyboard_unification/nonsticky-baseline.json').read_text());kind='portrait' if orient in [0,2] else 'landscape'
     assert hashlib.sha256(img.convert('RGB').tobytes()).hexdigest()==baseline['pixel_RGB_sha256_'+kind],name
    captures.append(dict(case=name,exit=rc,screenshot=str(png),sha256=sha(png),size=img.size))
 for raw in sorted(O.glob('*.pbm')):
  img=Image.open(raw);png=raw.with_suffix('.png');img.save(png)
  captures.append(dict(case=raw.stem,exit=next(r['exit']for r in rows if r['name']==raw.stem.split('--')[0]),screenshot=str(png),sha256=sha(png),size=img.size,boundary='actual SDL HAL backing framebuffer; physical panel orientation'))
 dump('captures.json',captures)
 runtime=[r for r in rows if not r['name'].startswith('compile-') and r['name']!='link']
 changed_inputs=[p for p,h in inputs.items() if sha(p)!=h]
 dump('final-input-rehash.json',dict(checked=len(inputs),changed=changed_inputs))
 dump('bindings.json',inputs)
 frame_names={c['case']for c in captures if '--'not in c['case']}
 successful=not changed_inputs and bool(runtime) and all(r['exit']==0 for r in rows) and frame_names=={r['name']for r in runtime}
 dump('RESULTS.json',dict(phase=a.phase,sanitize=a.sanitize,commands=rows,captures=captures,successful=successful,runtime_identities=[r['name']for r in runtime],boundary='whole production native keyboard/numeric/settings render+loop/HAL; production Lua editor/pad handlers and extracted native draw callbacks; isolated Activity stack/render mutex, header alias; no full app navigation'))
 raise SystemExit(not successful)
