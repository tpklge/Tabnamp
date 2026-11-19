#!/usr/bin/env python3
"""Consolidate current host and device evidence without treating emulation as hardware."""
from pathlib import Path
import hashlib
import json

root=Path(__file__).resolve().parents[1]
artifacts=root/'artifacts'
def newest(*paths):
    candidates=[root/p for p in paths if (root/p).exists()]
    return max(candidates,key=lambda p:p.stat().st_mtime) if candidates else None
def read(path):
    return json.loads(path.read_text()) if path and path.exists() else {}
def checked_log(marker,*paths):
    path=newest(*paths)
    return 'passed' if path and marker in path.read_text(errors='replace') else 'unknown'

simulation=newest('.emulator-build/artifacts/report.json','artifacts/report.json')
report={'simulation':read(simulation),'accelerated_soak':read(simulation.parent/'soak.json') if simulation else {}}
app=root/'build/tabnamp.bin'
digest=hashlib.sha256(app.read_bytes()).hexdigest()
install_paths=[artifacts/name for name in ('hardware-install-final.json','hardware-ui-audio-install.json')]
install_path=next((path for path in sorted(install_paths,key=lambda p:p.stat().st_mtime if p.exists() else 0,reverse=True)
                   if read(path).get('sha256')==digest),None)
install=read(install_path)
current_install=install.get('sha256')==digest
report['firmware']={'bytes':app.stat().st_size,'sha256':digest,'built':True,'flashed':current_install,'application_only':True,'partition_table_required':False}
report['core_tests']=checked_log('100% tests passed','artifacts/ui-audio-core-tests.log','artifacts/review-core-tests.log','artifacts/core-tests.log')
report['tool_tests']=checked_log('\nOK','artifacts/ui-audio-tools-tests.log','artifacts/review-tools-tests.log','artifacts/tools-tests.log')
report['legacy_codec_tests']=checked_log('1528832 frames','artifacts/review-legacy-tests.log','artifacts/legacy-tests.log')
report['hardware']={}
if current_install:
    report['hardware']['install']=install
    required=('hardware-test-report','hardware-formats-report','hardware-large-file-report','hardware-radio-report')
    for stem in (*required,'hardware-endurance-report','hardware-radio-recovery-report'):
        name=stem+'.json'
        path=artifacts/name
        if path.exists() and path.stat().st_mtime>=install_path.stat().st_mtime:
            report['hardware'][name.removesuffix('.json')]=read(path)
    report['hardware_validated']=all(report['hardware'].get(k,{}).get('status')=='passed' for k in required)
else:
    report['hardware_validated']=False
focused=read(artifacts/'hardware-ui-audio-report.json')
report['focused_ui_audio_validated']=(current_install and focused.get('firmware_sha256')==digest and focused.get('status')=='passed')
if focused.get('firmware_sha256')==digest:report['hardware']['ui_audio_regression']=focused
local=report['hardware'].get('hardware-endurance-report',{})
radio=report['hardware'].get('hardware-radio-report',{})
report['endurance_validated']=(local.get('status')=='passed' and local.get('elapsed_seconds',0)>=3600
    and radio.get('status')=='passed' and radio.get('elapsed_seconds',0)>=1800)
report['endurance_profile']={'local_accumulated_seconds':local.get('elapsed_seconds',0),
    'local_continuous':local.get('continuous',False),'local_harness_pause_seconds':local.get('harness_pause_seconds',0),
    'radio_seconds':radio.get('elapsed_seconds',0),'local_backlight':'automatically off during soak',
    'note':'Accumulated device playback and scripted controller changes are distinct from an uninterrupted screen-on UI stress test.'}
checks=[report[k]=='passed' for k in ('core_tests','tool_tests','legacy_codec_tests')]
checks.append(report['simulation'].get('status')=='passed')
report['host_status']='passed' if all(checks) else 'incomplete'
checks.append(report['hardware_validated'])
if local:checks.append(report['endurance_validated'])
report['status']='passed' if all(checks) else 'incomplete'
report['limitations']=[
    'SDL/LVGL tests do not emulate Tab5 peripherals.',
    'Accelerated soak durations are distinct from device wall-clock measurements.',
    'Codec playback checks prove only the tested files/profiles, not every file in a codec family.',
    'See docs/STATUS.md for remaining physical endurance, wake, HID, hotplug and panel measurements.'
]
(artifacts/'test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'status':report['status'],'hardware_validated':report['hardware_validated'],
                  'endurance_validated':report['endurance_validated'],'firmware':report['firmware']},indent=2))
