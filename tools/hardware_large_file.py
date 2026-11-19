#!/usr/bin/env python3
"""Play a supplied large MP3 through the physical sink and check seeking/memory."""
import argparse,json,time
from pathlib import Path
from player_serial import Player

def test(port,uri,seconds,output,duration_ms=0,max_seek_seconds=0):
    output.mkdir(parents=True,exist_ok=True)
    report={'status':'running','uri':uri,'checks':[],'samples':[],'realtime':True}
    def save():(output/'hardware-large-file-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (output/'hardware-large-file-serial.log').open('a') as log:
        p=Player(port,log);initial=p.command('status')
        try:
            p.command('volume',25)
            if initial['mute']:p.command('mute')
            for _ in range((3-initial['repeat'])%3):p.command('repeat');time.sleep(.1)
            p.command('play',uri);time.sleep(3);s=p.command('status')
            assert s['state_id']==3 and s['output_peak']>0,s
            baseline=s['output_errors'];report['checks'].append({'check':'open_large_file','sample':s})
            for delta in (60000,180000):
                start=time.monotonic();before=p.command('status');p.command('seek',delta)
                while time.monotonic()-start<90:
                    time.sleep(.25);s=p.command('status',timeout=45)
                    if s['state_id']==3 and s['position_ms']>=before['position_ms']+delta:break
                    assert s['state_id']!=7,s
                else:raise TimeoutError('Seek did not complete')
                report['checks'].append({'check':'seek_'+str(delta),'latency_seconds':time.monotonic()-start,'sample':s});save()
                if max_seek_seconds:assert report['checks'][-1]['latency_seconds']<=max_seek_seconds,'Indexed MP3 seek exceeded the limit'
            # Repeat this source once to detect its natural EOF without advancing
            # into another entry in the user's playlist. Poll near the tail.
            for _ in range((2-p.command('status')['repeat'])%3):p.command('repeat');time.sleep(.1)
            p.command('play',uri);time.sleep(1);began=time.monotonic();previous=p.command('status');device_started=previous['uptime_ms'];start_position=previous['position_ms'];report['device_started_ms']=device_started
            audio_end=previous
            while (previous['uptime_ms']-device_started)/1000<seconds:
                near_end=duration_ms and previous['position_ms']>duration_ms-10000
                time.sleep(min(.2 if near_end else 5,max(.1,seconds-(time.monotonic()-began))))
                s=p.command('status');report['samples'].append(s)
                assert s['state_id'] in (1,2,3,6) and s['uri']==uri,s
                assert s['output_errors']==baseline,s
                if s['position_ms']+duration_ms/2<previous['position_ms']:
                    assert duration_ms and previous['position_ms']>=duration_ms-1000,previous
                    report['completed_track']=True;report['eof_detected_by']='natural repeat'
                    report['position_after_repeat_ms']=s['position_ms'];audio_end=previous;break
                if s['state_id']==6:
                    assert s['position_ms']>= (duration_ms-1000 if duration_ms else previous['position_ms']),s
                    report['completed_track']=True;audio_end=s;break
                assert s['output_writes']>previous['output_writes'],s
                previous=audio_end=s;save()
            report['elapsed_seconds']=(s['uptime_ms']-device_started)/1000
            report['host_elapsed_seconds']=time.monotonic()-began
            report['device_audio_ms']=audio_end['position_ms']
            report['expected_duration_ms']=duration_ms
            if duration_ms and seconds*1000>=duration_ms:
                assert report.get('completed_track'), 'Track did not finish in its real-time playback window'
                report['audio_wall_ratio']=(audio_end['position_ms']-start_position)/max(1,audio_end['uptime_ms']-device_started)
                assert .97<=report['audio_wall_ratio']<=1.03, 'Audio clock diverged from the device clock'
            assert len(report['samples'])>=2
            memory=[s['psram_free'] for s in report['samples'] if s['state_id']==3]
            report['psram_range_bytes']=max(memory)-min(memory) if memory else 0
            report['status']='passed'
        except BaseException as error:
            report['status']='failed';report['error']=str(error);raise
        finally:
            p.command('stop');p.command('volume',initial['volume'])
            for _ in range((initial['repeat']-p.command('status')['repeat'])%3):p.command('repeat');time.sleep(.1)
            if initial['mute']:p.command('mute')
            p.close();save()
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--uri',required=True)
    parser.add_argument('--duration-ms',type=int,default=0)
    parser.add_argument('--max-seek-seconds',type=float,default=0)
    parser.add_argument('--seconds',type=int,default=365);parser.add_argument('--output',type=Path,default=Path('artifacts'))
    a=parser.parse_args();test(a.port,a.uri,a.seconds,a.output,a.duration_ms,a.max_seek_seconds)
