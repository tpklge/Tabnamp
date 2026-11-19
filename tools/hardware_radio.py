#!/usr/bin/env python3
"""Serve original ICY MP3/HLS on the LAN and test the Tab5 Wi-Fi audio sink."""
import argparse,http.server,json,socket,struct,subprocess,threading,time
from pathlib import Path
from player_serial import Player

def test(port,ip,seconds,output,outage_seconds=0,outage_after=30):
    root=Path(__file__).resolve().parents[1]
    output.mkdir(parents=True,exist_ok=True)
    audio=output/'radio-original.mp3'
    if not audio.exists():
        subprocess.run(['ffmpeg','-v','error','-n','-i',str(root/'artifacts/hardware-media-audible/batidas.wav'),'-codec:a','libmp3lame','-b:a','128k','-write_xing','0','-id3v2_version','0',str(audio)],check=True)
    original=audio.read_bytes()
    active=set();lock=threading.Lock();cancel=threading.Event()
    outage={'until':0,'performed':False}
    class Handler(http.server.BaseHTTPRequestHandler):
        sent=0;connections=0;unavailable=0
        def log_message(self,*args):pass
        def do_GET(self):
            if self.path=='/live.mp3':
                if time.monotonic()<outage['until']:
                    Handler.unavailable+=1;self.send_error(503);return
                Handler.connections+=1
                with lock:active.add(self.connection)
                self.send_response(200);self.send_header('Content-Type','audio/mpeg');self.send_header('icy-metaint','16384');self.end_headers()
                at=0
                try:
                    while True:
                        data=bytearray()
                        while len(data)<16384:
                            count=min(16384-len(data),len(original)-at);data.extend(original[at:at+count]);at=(at+count)%len(original)
                        self.wfile.write(data+b'\x02'+b"StreamTitle='Batidas Tabnamp';".ljust(32,b'\0'));Handler.sent+=len(data)
                except OSError:pass
                finally:
                    with lock:active.discard(self.connection)
            elif self.path.startswith('/hls/') and '..' not in self.path:
                file=root/'.emulator-build/fixtures'/self.path.lstrip('/')
                if not file.is_file():self.send_error(404);return
                data=file.read_bytes();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
                try:self.wfile.write(data)
                except (BrokenPipeError,ConnectionResetError):pass
            else:self.send_error(404)
    server=http.server.ThreadingHTTPServer(('0.0.0.0',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    def interrupt_server():
        if cancel.wait(outage_after):return
        outage.update(until=time.monotonic()+outage_seconds,performed=True)
        with lock:connections=list(active)
        for connection in connections:
            try:
                # Drop queued TCP output as well; a graceful FIN can otherwise
                # keep feeding the player throughout the intended outage.
                connection.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0))
                connection.shutdown(socket.SHUT_RDWR);connection.close()
            except OSError:pass
    outage_thread=threading.Thread(target=interrupt_server,daemon=True)
    if outage_seconds:outage_thread.start()
    base='http://'+ip+':'+str(server.server_port)
    report={'status':'running','seconds_requested':seconds,'samples':[],'hls':[],'server':base,'synthetic_audio':True,'outage_seconds_requested':outage_seconds}
    def save():(output/'hardware-radio-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (output/'hardware-radio-serial.log').open('a') as log:
        p=Player(port,log);initial=p.command('status')
        try:
            p.command('volume',25)
            if initial['mute']:p.command('mute')
            p.command('play',base+'/live.mp3')
            ready_until=time.monotonic()+45
            while True:
                time.sleep(.5);before=p.command('status')
                if before['state_id']==3 and before['output_peak']>0 and before['position_ms']>=500:break
                assert before['state_id'] in (1,2,3),before
                if time.monotonic()>=ready_until:raise TimeoutError('Radio did not start after network initialization')
            report['before']=before;began=time.monotonic()
            p.command('seek',1000);time.sleep(.5);live=p.command('status')
            assert live['state_id']==3 and live['position_ms']>before['position_ms'],live
            report['live_seek_rejected_without_stopping']=True
            previous=before;audio_ms=0
            while time.monotonic()-began<seconds:
                time.sleep(3);s=p.command('status');report['samples'].append(s)
                assert s['state_id'] in (1,2,3) and s['uri']==base+'/live.mp3',s
                assert s['output_errors']==before['output_errors'],s
                delta=s['position_ms']-previous['position_ms']
                audio_ms+=delta if delta>=0 else s['position_ms']
                previous=s
                report['elapsed_seconds']=(s['uptime_ms']-before['uptime_ms'])/1000
                report['connections']=Handler.connections;report['http_503_during_outage']=Handler.unavailable
                report['outage_performed']=outage['performed']
                save()
            assert s['output_writes']>before['output_writes']+100,s
            report['elapsed_seconds']=time.monotonic()-began;report['server_bytes']=Handler.sent
            report['audio_wall_ratio']=audio_ms/max(1,s['uptime_ms']-before['uptime_ms'])
            assert report['audio_wall_ratio']>=(.90 if not outage_seconds or seconds>=outage_seconds*5 else .45), 'Live audio stalled or ran slower than real time'
            report['outage_performed']=outage['performed'];report['connections']=Handler.connections;report['http_503_during_outage']=Handler.unavailable
            report['buffering_seen']=any(x['state_id']==2 for x in report['samples'])
            if outage_seconds:
                report['recovered_after_outage']=outage['performed'] and Handler.connections>=2 and s['state_id']==3
                assert report['recovered_after_outage'],report
            for route in ('adts','mpegts'):
                before_hls=p.command('status')
                target=base+'/hls/'+route+'/audio.m3u8';load_started=time.monotonic();p.command('play',target)
                while True:
                    time.sleep(.5);sample=p.command('status')
                    passed=(sample['state_id']==3 and sample['uri']==target and sample['position_ms']>1000 and sample['output_peak']>0
                            and sample['output_writes']>before_hls['output_writes']
                            and sample['output_errors']==before_hls['output_errors'])
                    if passed or sample['state_id'] in (6,7) or time.monotonic()-load_started>=30:break
                report['hls'].append({'route':route,'passed':passed,'sample':sample,'startup_seconds':time.monotonic()-load_started});save()
            report['status']='passed' if all(x['passed'] for x in report['hls']) else 'incomplete'
        except BaseException as error:
            report['status']='failed';report['error']=str(error);raise
        finally:
            p.command('stop');p.command('volume',initial['volume'])
            if initial['mute']:p.command('mute')
            p.close();cancel.set();server.shutdown();server.server_close();thread.join()
            if outage_seconds:outage_thread.join()
            save()
    print(json.dumps(report,ensure_ascii=False,indent=2))
    if report['status']!='passed':raise RuntimeError('Hardware radio/HLS validation incomplete; inspect the report')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--ip',required=True)
    parser.add_argument('--seconds',type=int,default=60);parser.add_argument('--output',type=Path,default=Path('artifacts'))
    parser.add_argument('--outage-seconds',type=int,default=0);parser.add_argument('--outage-after',type=int,default=30)
    a=parser.parse_args();test(a.port,a.ip,a.seconds,a.output,a.outage_seconds,a.outage_after)
