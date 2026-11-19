#!/usr/bin/env python3
"""Run a real-time local soak with repeated playback, EQ, volume and orientation.

Requires a settings snapshot read from this device before the run, so all changed
preferences can be restored. Does not simulate touch, HID, wake or DMA metrics.
"""
import argparse
import json
import time
from pathlib import Path
from player_serial import Player


def run(port, uri, seconds, duration_ms, settings, output, resume=False):
    assert seconds > 0 and duration_ms > 0
    config=json.loads(settings.read_text())
    assert isinstance(config['eq_enabled'],bool) and len(config['bands_db'])==10
    output.mkdir(parents=True,exist_ok=True)
    report=dict(status='running',realtime=True,uri=uri,seconds_requested=seconds,
                samples=[],interactions=[],completed_tracks=0,audio_ms=0)
    elapsed_base=0
    if resume:
        previous_report=json.loads((output/'hardware-endurance-report.json').read_text())
        assert previous_report['uri']==uri and previous_report['seconds_requested']==seconds
        assert previous_report['status']=='failed' and "'state_id': 1" in previous_report.get('error',''), 'Only the known loading-state harness interruption can resume'
        (output/'hardware-endurance-harness-interruption.json').write_text(json.dumps(previous_report,ensure_ascii=False,indent=2)+'\n')
        report=previous_report;elapsed_base=report['elapsed_seconds']
        report.setdefault('harness_interruptions',[]).append(dict(reason=report.pop('error'),validated_seconds_before_pause=elapsed_base))
        report['status']='running';report['continuous']=False
        if not report.get('segments'):
            report['segments']=[dict(start_uptime_ms=report['before']['uptime_ms'],end_uptime_ms=report['samples'][-1]['uptime_ms'],accumulated_seconds_at_start=0,validated_seconds=elapsed_base)]
    def save():
        (output/'hardware-endurance-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (output/'hardware-endurance-serial.log').open('a') as log:
        p=Player(port,log)
        initial=p.command('status')
        try:
            p.command('volume',25)
            if initial['mute']:p.command('mute')
            if not config['eq_enabled']:p.command('bypass')
            for _ in range((2-initial['repeat'])%3):p.command('repeat');time.sleep(.1)
            p.command('play',uri);time.sleep(3)
            before=previous=p.command('status');assert before['state_id']==3 and before['output_peak']>0,before
            if resume:assert before['uptime_ms']>report['samples'][-1]['uptime_ms'],'Device rebooted between segments'
            else:report['before']=before;report['continuous']=True
            report.setdefault('segments',[]).append(dict(start_uptime_ms=before['uptime_ms'],accumulated_seconds_at_start=elapsed_base))
            next_interaction=(int(elapsed_base//30)+1)*30;step=len(report['interactions']);last_progress=before['uptime_ms']
            while elapsed_base+(previous['uptime_ms']-before['uptime_ms'])/1000<seconds:
                time.sleep(5);s=p.command('status')
                elapsed=elapsed_base+(s['uptime_ms']-before['uptime_ms'])/1000
                assert s['uptime_ms']>previous['uptime_ms'],s
                assert s['uri']==uri and s['state_id'] in (1,2,3),s
                assert s['output_errors']==before['output_errors'],s
                if s['output_writes']>previous['output_writes']:last_progress=s['uptime_ms']
                assert s['uptime_ms']-last_progress<15000,'No output progress for 15 seconds'
                assert s['eq_enabled'],s
                if report['interactions']:
                    expected=report['interactions'][-1]
                    assert s['orientation_mode']==expected['orientation'] and s['bands_db'][5]==expected['band_5_db'],s
                delta=s['position_ms']-previous['position_ms']
                if delta<0:
                    delta+=duration_ms;report['completed_tracks']+=1
                report['audio_ms']+=delta
                report['samples'].append(s);report['elapsed_seconds']=elapsed
                report['audio_wall_ratio']=report['audio_ms']/max(1,elapsed*1000)
                report['wall_elapsed_including_harness_pause_seconds']=(s['uptime_ms']-report['before']['uptime_ms'])/1000
                if elapsed>=next_interaction:
                    step+=1;next_interaction+=30
                    mode=1 if step%2 else 2
                    band_gain=-3 if step%2 else 0
                    p.command('rotate',mode);p.command('volume',25 if step%2 else 30)
                    p.command('band',5,band_gain)
                    p.command('browse','/usb' if step%2 and s['usb_mounted'] else '/sdcard')
                    report['interactions'].append(dict(at_seconds=elapsed,orientation=mode,
                                                      band_5_db=band_gain,operation='rotate/volume/EQ/browse'))
                previous=s;save()
            assert report['audio_wall_ratio']>=.97,report['audio_wall_ratio']
            report['psram_range_bytes']=max(x['psram_free'] for x in report['samples'])-min(x['psram_free'] for x in report['samples'])
            report['internal_min_sampled']=min(x['internal_free'] for x in report['samples'])
            report['status']='passed'
        except BaseException as error:
            report['status']='failed';report['error']=str(error);raise
        finally:
            try:
                p.command('stop');p.command('volume',initial['volume']);p.command('rotate',initial['orientation_mode'])
                p.command('band',5,int(config['bands_db'][5]))
                if not config['eq_enabled']:p.command('bypass')
                for _ in range((initial['repeat']-p.command('status')['repeat'])%3):p.command('repeat');time.sleep(.1)
                if initial['mute']:p.command('mute')
            finally:p.close();save()
    print(json.dumps({k:v for k,v in report.items() if k not in ('samples','interactions')},ensure_ascii=False,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--uri',required=True)
    parser.add_argument('--seconds',type=int,default=3600);parser.add_argument('--duration-ms',type=int,required=True)
    parser.add_argument('--settings',type=Path,required=True);parser.add_argument('--output',type=Path,default=Path('artifacts'))
    parser.add_argument('--resume',action='store_true',help='Complete the same evidence window after the recorded loading-state harness interruption; explicitly reports noncontinuous segments')
    a=parser.parse_args();run(a.port,a.uri,a.seconds,a.duration_ms,a.settings,a.output,a.resume)
