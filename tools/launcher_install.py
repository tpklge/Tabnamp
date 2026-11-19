#!/usr/bin/env python3
"""Install the application through Paludo Launcher, using its verified installation plan.

Uses the launcher's local serial client for authentication. Binary transfer and
installation use the launcher's authenticated HTTP API. Never uses esptool.
"""
import argparse
import hashlib
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

def install(port, app, launcher_tools, report, allow_allocation=False):
    data=app.read_bytes()
    if len(data)<288 or data[0]!=0xe9 or int.from_bytes(data[12:14],'little')!=0x12:
        raise ValueError('Expected ESP32-P4 application-only image')
    if data[23]!=1 or hashlib.sha256(data[:-32]).digest()!=data[-32:]:
        raise ValueError('Application appended SHA-256 is invalid')
    sys.path.insert(0,str(launcher_tools))
    from tab5_usb import Tab5
    def authenticate():
        for attempt in range(12):
            device=None
            try:
                device=Tab5(port,timeout=3)
                return device.command('web-start')
            except (OSError,TimeoutError,RuntimeError):
                if attempt==11:raise
                time.sleep(1)
            finally:
                if device:device.close()
    auth=authenticate()
    def request(path,body=None):
        headers={'X-Launcher-Token':auth['code']}
        if isinstance(body,dict):
            body=json.dumps(body).encode();headers['Content-Type']='application/json'
        elif body is not None:headers['Content-Type']='application/octet-stream'
        req=urllib.request.Request('http://'+auth['ip']+path,data=body,headers=headers)
        with urllib.request.urlopen(req,timeout=120 if body is not None else 15) as response:
            return json.loads(response.read())
    before=request('/api/flash-layout')
    previous_apps=request('/api/apps')
    digest=hashlib.sha256(data).hexdigest()
    path='/sdcard/tabnamp/firmware/tabnamp-'+digest[:12]+'.bin'
    upload=request('/api/upload?'+urllib.parse.urlencode({'directory':'/sdcard/tabnamp/firmware','relative':Path(path).name}),data)
    plan=request('/api/operation',{'action':'install','path':path})
    if not plan.get('ok') or not plan.get('ticket'):
        raise RuntimeError(plan)
    if plan.get('willRestart') and not allow_allocation:
        raise RuntimeError('The launcher requires layout changes; existing layout left unchanged: '+str(plan))
    print(plan['message'],flush=True)
    started=request('/api/operation',{'action':'execute','ticket':plan['ticket']})
    if not started.get('ok'):raise RuntimeError(started)
    deadline=time.monotonic()+240
    rebooted=False
    apps=[]
    occupied={a['slot'] for a in previous_apps if a.get('descriptorValid')}
    after=None
    while time.monotonic()<deadline:
        try:
            state=request('/api/operation')
            apps=request('/api/apps')
            if any(a.get('name')=='tabnamp' and a['slot'] not in occupied for a in apps):
                if not state.get('running'):
                    try:after=request('/api/flash-layout')
                    except urllib.error.HTTPError as busy:
                        if busy.code!=409:raise
                    if after is not None:break
            if not plan.get('willRestart') and not state.get('running') and state.get('error'):
                raise RuntimeError(state)
        except (OSError, urllib.error.URLError):
            if not allow_allocation:raise
            # The launcher resumes installation from its checked SD copy after reboot.
            time.sleep(2)
            try:auth=authenticate();rebooted=True
            except (OSError,TimeoutError,RuntimeError):pass
        time.sleep(1)
    else:raise TimeoutError('Launcher installation not confirmed; inspect pending operation on device')
    changed=before['sizes']!=after['sizes']
    if changed and not allow_allocation:raise RuntimeError('Unexpected layout change')
    for previous in previous_apps:
        if previous.get('descriptorValid'):
            if not any(a.get('name')==previous['name'] and a.get('version')==previous['version'] for a in apps):
                raise RuntimeError('Previous application missing after installation')
    result={'bytes':len(data),'sha256':digest,'application_only':True,'partition_table_changed':changed,'allocation_by_launcher':changed,'rebooted':rebooted,'before_apps':previous_apps,'after_apps':apps,'layout':after,'plan':plan['message'],'install':state,'upload':upload}
    report.parent.mkdir(parents=True,exist_ok=True)
    report.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return apps

if __name__=='__main__':
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--allow-launcher-allocation',action='store_true',help='Allow the launcher to resize/move slots with its verified recovery mechanism; make a full device backup first')
    parser.add_argument('--app',type=Path,default=root/'build/tabnamp.bin')
    parser.add_argument('--launcher-tools',type=Path,default=root.parent/'Tab5LauncherPaludo/tools')
    parser.add_argument('--report',type=Path,default=root/'artifacts/hardware-install.json')
    a=parser.parse_args()
    install(a.port,a.app,a.launcher_tools,a.report,a.allow_launcher_allocation)
