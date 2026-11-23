#!/usr/bin/env python3
"""Routine Crosscub host qualification. No firmware build/install/deployment.
Supply existing read-only dependency/fixture paths via environment; no downloads.
"""
from pathlib import Path
import argparse,json,os,subprocess,sys,time,hashlib
R=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--reader-only',action='store_true');p.add_argument('--airdrop-only',action='store_true');p.add_argument('--ota-only',action='store_true');p.add_argument('--heatmap-only',action='store_true');a=p.parse_args();O=a.output.resolve();O.mkdir(parents=True,exist_ok=True)
heatmap_commands=[[sys.executable,str(R/'test/reading_heatmap/run.py')]+(['--sanitize'] if sanitized else []) for sanitized in [False,True]]
ota_commands=[
 [sys.executable,str(R/'test/ota_transition/run.py'),'--output',str(O/('ota-ubsan' if sanitized else 'ota-normal'))]+(['--sanitize'] if sanitized else [])
 for sanitized in [False,True]
]+[
 [sys.executable,str(R/'test/ota_bootstrap/run.py')],
 [sys.executable,'-m','unittest','discover','-s',str(R/'test/ota_release'),'-v'],
]
if a.ota_only:
 rows=[]
 for index,command in enumerate(ota_commands):
  log=O/f'ota-{index:02d}.log'
  with log.open('w') as f:
   run=subprocess.run(command,cwd=R,stdout=f,stderr=subprocess.STDOUT,timeout=180)
  rows.append({'command':command,'exit':run.returncode,'log':str(log)})
  print(index,run.returncode,flush=True)
 (O/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
 raise SystemExit(any(row['exit'] for row in rows))
if a.heatmap_only:
 rows=[]
 for index,command in enumerate(heatmap_commands):
  log=O/f'heatmap-{index:02d}.log'
  with log.open('w') as f:
   run=subprocess.run(command,cwd=R,stdout=f,stderr=subprocess.STDOUT,timeout=90)
   f.write(f'EXIT={run.returncode}\n')
  rows.append({'command':command,'exit':run.returncode,'log':str(log)})
  print(index,run.returncode,flush=True)
 (O/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
 raise SystemExit(any(row['exit'] for row in rows))
airdrop_commands=[]
for sanitized in [False,True]:
 for suite in ['fixes','session','stream','storage']:
  command=[sys.executable,str(R/f'test/airdrop/{suite}_run.py')]+(['--sanitize'] if sanitized else [])
  if suite!='fixes':command=['flock','-n','/root/.hermes/cache/scratch/crosscub-review-probes.lock',*command]
  airdrop_commands.append(command)
if a.airdrop_only:
 rows=[]
 for index,command in enumerate(airdrop_commands):
  log=O/f'airdrop-{index:02d}.log';start=time.monotonic()
  with log.open('w') as f:
   f.write('COMMAND '+json.dumps(command)+'\n');f.flush()
   try:code=subprocess.run(command,cwd=R,stdout=f,stderr=subprocess.STDOUT,timeout=180,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'}).returncode
   except subprocess.TimeoutExpired:code=124
   f.write(f'EXIT={code}\n')
  rows.append({'command':command,'exit':code,'log':str(log),'seconds':round(time.monotonic()-start,2)})
  (O/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(index,code,flush=True)
 raise SystemExit(any(row['exit'] for row in rows))
reader_commands=[
 [sys.executable,str(R/'test/epub_reader_regressions/run.py'),'--case',str(case),'--output',str(O/'reader-probe')]+(['--sanitize'] if sanitized else [])
 for sanitized in [False,True] for case in range(1,6)
]
if a.reader_only:
 rows=[]
 for index,command in enumerate(reader_commands):
  log=O/f'reader-{index:02d}.log'
  with log.open('w') as f:
   f.write('COMMAND '+json.dumps(command)+'\n');f.flush()
   run=subprocess.run(command,cwd=R,stdout=f,stderr=subprocess.STDOUT,timeout=90)
   f.write(f'EXIT={run.returncode}\n')
  rows.append({'command':command,'exit':run.returncode,'log':str(log)})
  print(index,run.returncode,flush=True)
 (O/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
 raise SystemExit(any(row['exit'] for row in rows))
try:import PIL
except ImportError:raise SystemExit('Use an existing Python with Pillow (2048 capture export); this runner never installs packages')
for key in ['ARDUINOJSON_INCLUDE_DIR','CP_FIXTURES','ESP_IDF_SOURCE_ROOT']:
 if not os.environ.get(key)or not Path(os.environ[key]).is_dir():raise SystemExit('Set '+key+' to the existing read-only dependency/fixture directory')
env={**os.environ,'ASSISTED_TEST_OUT':str(O/'browser'),'AIRDROP_SESSION_TEST_OUTPUT':str(O/'receiver'),'PYTHONDONTWRITEBYTECODE':'1'}
env.setdefault('BROWSER_CODEC_ROOT',str(Path(env['ARDUINOJSON_INCLUDE_DIR']).resolve().parents[1]))
commands=heatmap_commands+ota_commands+airdrop_commands+reader_commands+[[sys.executable,str(R/'test/file_actions/run_all.py'),'--output',str(O/'file-actions')]]+[[sys.executable,str(R/'test/crosscub'/('test_'+name+'.py'))]for name in ['product','web','release','ota_activity','logo','stopwatch']]
commands+=[
 [sys.executable,str(R/'test/integration_catalog/check_catalog.py')],
 [sys.executable,str(R/'test/crosscub/run_preservation.py'),'--group','apps','--output',str(O/'apps')],
 [sys.executable,str(R/'test/crosscub/run_preservation.py'),'--group','core','--output',str(O/'core')],
 [sys.executable,str(R/'test/crossplay_ui_parity/build_capture.py'),'--output',str(O/'raster')],
 [sys.executable,str(R/'test/crosscub/run_ui.py'),'--runner',str(O/'raster/capture-runner'),'--output',str(O/'ui')],
 [sys.executable,str(R/'test/crosscub/run_stock_ui.py'),'--output',str(O/'stock')],
 [sys.executable,str(R/'test/utility_sudoku_original_ui/run.py'),'--work-dir',str(O/'utility-sudoku'),'--runner',str(O/'raster/capture-runner')],
 [sys.executable,str(R/'test/chess_original_ui/run.py'),'--work-dir',str(O/'chess'),'--runner',str(O/'raster/capture-runner')],
 [sys.executable,str(R/'test/hackernews_original_ui/run.py'),'--work-dir',str(O/'hackernews')],
 [sys.executable,str(R/'test/crosscub/test_logo_actual.py'),'--runner',str(O/'raster/capture-runner'),'--output',str(O/'boot')],
]
def inputs():
 result={}
 for base in ['src','include','lib','apps','web']:
  for path in (R/base).rglob('*'):
   if path.is_file()and'.generated.'not in path.name:result[str(path.relative_to(R))]=hashlib.sha256(path.read_bytes()).hexdigest()
 for name in ['platformio.ini','partitions.csv','sdkconfig.defaults']:result[name]=hashlib.sha256((R/name).read_bytes()).hexdigest()
 return result
before=inputs();rows=[]
for index,command in enumerate(commands):
 log=O/f'{index:02d}-{Path(command[1]).stem}.log';start=time.monotonic()
 try:
  with log.open('w')as f:run=subprocess.run(command,cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=1200)
  code=run.returncode
 except subprocess.TimeoutExpired:code=124
 rows.append({'command':command,'exit':code,'log':str(log),'seconds':round(time.monotonic()-start,2)})
 (O/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(index,code,command[1],flush=True)
after=inputs();stable=before==after;(O/'source-stability.json').write_text(json.dumps({'stable':stable,'files':after},indent=2)+'\n')
raise SystemExit(any(row['exit']for row in rows)or not stable)
