#!/usr/bin/env python3
"""Validate local formats and one existing USB audio file on the running Tab5."""
import argparse
import json
import time
from pathlib import Path
from player_serial import Player

def run(port, output):
    output.mkdir(parents=True,exist_ok=True)
    result={'status':'running','formats':[],'usb':{'tested':False},'acoustic_confirmation':False}
    with (output/'hardware-formats-serial.log').open('a') as log:
        player=Player(port,log)
        initial=player.command('status')
        try:
            player.command('volume',25)
            if initial['mute']:player.command('mute')
            for name in ('batidas.flac','batidas.ogg','batidas.opus','batidas.aac','batidas.m4a','batidas.wma','batidas-v1.wma','batidas-vbr.mp3','batidas-320.mp3','atrac3-reference.at3','atrac3plus-reference.at3'):
                before=player.command('status');player.command('play','/sdcard/tabnamp/music/testes-20261119/'+name)
                time.sleep(3);s=player.command('status')
                passed=s['state_id']==3 and s['position_ms']>=1000 and s['output_writes']>before['output_writes'] and s['output_errors']==before['output_errors'] and s['output_peak']>0
                result['formats'].append({'file':name,'passed':passed,'sample':s})
                if name in ('batidas.m4a','batidas-vbr.mp3') and passed:
                    player.command('seek',2000);time.sleep(1.5);seek=player.command('status')
                    result['m4a_seek' if name=='batidas.m4a' else 'vbr_seek']={'passed':seek['state_id']==3 and seek['position_ms']>=4500,'sample':seek}
                print(name,passed,s['message'],flush=True)
                (output/'hardware-formats-report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2))
            player.command('stop');time.sleep(.2)
            if player.command('status')['usb_mounted']:
                pending=['/usb'];seen=set();listing=[];candidate=None
                while pending and len(seen)<80 and not candidate:
                    folder=pending.pop(0)
                    if folder in seen:continue
                    seen.add(folder);player.command('browse',folder);time.sleep(.4)
                    page=0
                    while page<100:
                        entries=player.command('files',page);listing.append(entries)
                        for entry in entries['entries']:
                            path=entry['path']
                            if Path(path).name=='..':continue
                            if entry['directory']:pending.append(path)
                            elif path.lower().endswith(('.mp3','.wav','.flac','.ogg','.opus','.aac','.m4a','.wma','.at3','.oma','.aa3')):
                                candidate=path;break
                        if candidate or page+20>=entries['total']:break
                        page+=20
                result['usb']['listing']=listing
                if candidate:
                    before=player.command('status');player.command('play',candidate);time.sleep(4);s=player.command('status')
                    passed=s['state_id']==3 and s['position_ms']>1000 and s['output_peak']>0 and s['output_writes']>before['output_writes'] and s['output_errors']==before['output_errors']
                    result['usb'].update({'tested':True,'passed':passed,'uri':candidate,'sample':s})
                    if passed and candidate.lower().endswith('.mp3'):
                        player.command('seek',2000);time.sleep(2);seek=player.command('status')
                        result['usb']['seek_passed']=seek['state_id']==3 and seek['position_ms']>=s['position_ms']+2000
                        result['usb']['seek_sample']=seek
                    print('USB',candidate,passed,flush=True)
                else:result['usb']['reason']='No audio found within the bounded USB search'
            else:result['usb']['reason']='No mounted FAT USB drive'
            result['status']='passed' if (all(x['passed'] for x in result['formats'])
                and result.get('m4a_seek',{}).get('passed',False)
                and result.get('vbr_seek',{}).get('passed',False)
                and result['usb'].get('passed',False)) else 'incomplete'
            if result['usb'].get('seek_passed') is False:result['status']='incomplete'
        finally:
            player.command('stop');player.command('volume',initial['volume'])
            if initial['mute']:player.command('mute')
            player.close()
            (output/'hardware-formats-report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(result,ensure_ascii=False,indent=2))
    if result['status']!='passed':raise RuntimeError('Hardware formats/USB validation incomplete; inspect the report')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--output',type=Path,default=Path('artifacts'))
    a=parser.parse_args();run(a.port,a.output)
