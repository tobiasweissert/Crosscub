#!/usr/bin/env python3
"""Bounded entrypoint for this bridge only; never invokes a firmware build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
TESTS=['test_clock.py','test_storage.py','test_touch_snapshot.py','test_overlay.py','test_publication_page.py','test_secure_client.py','test_ui_host.py','test_input.py','test_themes.py','test_activity_results.py','test_build_contract.py']
NATIVE=set(TESTS)-{'test_overlay.py','test_build_contract.py'}
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--mode',choices=['normal','sanitized'],default='normal');parser.add_argument('--evidence',type=Path,required=True);args=parser.parse_args()
    here=Path(__file__).resolve().parent;args.evidence.mkdir(parents=True,exist_ok=True)
    selected=[t for t in TESTS if args.mode=='normal' or t in NATIVE]
    results=[]
    for test in selected:
        command=[sys.executable,str(here/test)];env=dict(os.environ,SANITIZE='1' if args.mode=='sanitized' else '0',PYTHONDONTWRITEBYTECODE='1')
        try:
            p=subprocess.run(command,capture_output=True,text=True,timeout=40,env=env)
            code=p.returncode;output=p.stdout+p.stderr
        except subprocess.TimeoutExpired as ex:
            code=124;output='TIMEOUT '+str(ex)
        log=args.evidence/(args.mode+'-'+test+'.log');log.write_text(output)
        results.append({'test':test,'command':command,'exit':code,'log':str(log),'test_sha256':hashlib.sha256((here/test).read_bytes()).hexdigest()})
        (args.evidence/(args.mode+'-results.json')).write_text(json.dumps({'mode':args.mode,'expected':selected,'results':results,'complete':len(results)==len(selected)},indent=2)+'\n')
        print(args.mode,test,code,flush=True)
    assert len(results)==len(selected)
    return int(any(r['exit'] for r in results))
if __name__=='__main__':sys.exit(main())
