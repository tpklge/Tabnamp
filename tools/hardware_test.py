#!/usr/bin/env python3
"""Exercise the running Tab5 audio sink with original MP3/WAV fixtures.

Reports codec writes and nonzero PCM; acoustic output requires a listener.
"""
import argparse
import json
import time
from pathlib import Path
from player_serial import Player

def test(port, output, seconds):
    output.mkdir(parents=True,exist_ok=True)
    report={'device':'M5Stack Tab5','status':'running','acoustic_confirmation':False,'checks':[],'samples':[]}
    def record(label,data):
        report['checks'].append({'check':label,'status':'passed','sample':data})
        (output/'hardware-test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (output/'hardware-player-serial.log').open('a') as log:
        p=Player(port,log)
        try:
            initial=p.command('status');record('serial_application_ready',initial)
            volume=initial['volume'];mute=initial['mute'];orientation=initial['orientation_mode']
            p.command('volume',25)
            if mute:p.command('mute')
            for name in ('apitos.mp3','batidas.mp3','estereo.mp3','batidas.wav'):
                before=p.command('status')
                p.command('play','/sdcard/tabnamp/music/testes-20261119/'+name)
                time.sleep(3)
                s=p.command('status')
                assert s['state_id']==3,s
                assert s['position_ms']>=1000 and s['output_writes']>before['output_writes'],s
                assert s['output_errors']==before['output_errors'] and s['output_peak']>0,s
                record('physical_codec_output_'+name,s)
            p.command('pause');time.sleep(.5);paused=p.command('status');time.sleep(1);s=p.command('status')
            assert s['state_id']==4 and s['position_ms']==paused['position_ms'],s
            record('pause_holds_timeline',s)
            p.command('play');time.sleep(.5);p.command('seek',2000);time.sleep(1);s=p.command('status')
            assert s['state_id']==3 and s['position_ms']>=4000,s
            record('physical_wav_seek',s)
            for mode in (2,1):
                p.command('rotate',mode);time.sleep(.5);s=p.command('status')
                assert s['state_id']==3 and s['orientation_mode']==mode and s['landscape']==(mode==1),s
                record('rotation_preserves_output_'+str(mode),s)
            p.command('play','/sdcard/tabnamp/music/testes-20261119/batidas.mp3');time.sleep(1)
            eq_before=p.command('status')
            try:
                p.command('band',5,-3);time.sleep(.5);s=p.command('status')
                assert s['state_id']==3 and s['bands_db'][5]==-3 and s['output_errors']==eq_before['output_errors'],s
                record('eq_band_change_preserves_output',s)
                p.command('bypass');time.sleep(.5);s=p.command('status')
                assert s['state_id']==3 and s['eq_enabled']!=eq_before['eq_enabled'],s
                record('eq_bypass_preserves_output',s)
                p.command('bypass')
            finally:p.command('band',5,eq_before['bands_db'][5])
            p.command('stop');time.sleep(.5);stopped=p.command('status');time.sleep(.5);s=p.command('status')
            assert s['state_id']==6 and s['output_writes']==stopped['output_writes'],s
            record('stop_drains_output',s)
            p.command('play','/sdcard/tabnamp/music/testes-20261119/missing.mp3');time.sleep(.5);s=p.command('status')
            assert s['state_id']==7,s
            record('missing_media_reports_error',s)
            p.command('play','/sdcard/tabnamp/music/testes-20261119/batidas.mp3');time.sleep(1)
            # Repeat-one; poll until the engine has processed each queued command.
            # Use the known boot setting from the device, returned in status.
            for _ in range((2-p.command('status').get('repeat',0))%3):
                p.command('repeat');time.sleep(.1)
            began=time.monotonic();before=p.command('status')
            while time.monotonic()-began<seconds:
                time.sleep(min(5,max(.1,seconds-(time.monotonic()-began))))
                s=p.command('status');report['samples'].append(s)
                assert s['state_id'] in (2,3),s
                assert s['output_errors']==before['output_errors'],s
                (output/'hardware-test-progress.json').write_text(json.dumps({'elapsed_seconds':round(time.monotonic()-began,1),'target_seconds':seconds,'sample':s},ensure_ascii=False,indent=2))
            record('realtime_mp3_soak',{'elapsed_seconds':time.monotonic()-began,'before':before,'after':s})
            p.command('stop');p.command('volume',volume);p.command('rotate',orientation)
            current_repeat=p.command('status')['repeat']
            for _ in range((initial['repeat']-current_repeat)%3):
                p.command('repeat');time.sleep(.1)
            if mute:p.command('mute')
            report['status']='passed';report['usb_mounted']=s['usb_mounted']
        except BaseException as error:
            report['status']='failed';report['error']=str(error)
            raise
        finally:
            p.close()
            (output/'hardware-test-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--output',type=Path,default=Path('artifacts'))
    parser.add_argument('--seconds',type=int,default=120)
    args=parser.parse_args();test(args.port,args.output,args.seconds)
