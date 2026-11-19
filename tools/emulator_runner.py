#!/usr/bin/env python3
"""Run the simulator integration suite against a local HTTP test server."""
import http.server,threading,subprocess,os,sys,json,ssl,tempfile
from pathlib import Path
binary,fixtures,artifacts=sys.argv[1:4]
root=Path(fixtures).resolve()
class Handler(http.server.BaseHTTPRequestHandler):
    unstable=0
    def log_message(self,*args): pass
    def do_GET(self):
        path=self.path.split('?',1)[0]
        if path=='/live.mp3':
            self.send_response(200);self.send_header('Content-Type','audio/mpeg');self.send_header('icy-metaint','16384');self.send_header('icy-name','Tabnamp local');self.end_headers()
            audio=(root/'live.mp3').read_bytes();at=0
            try:
                while True:
                    block=bytearray()
                    while len(block)<16384:
                        count=min(16384-len(block),len(audio)-at);block.extend(audio[at:at+count]);at=(at+count)%len(audio)
                    metadata=b"StreamTitle='Radio sintetica';";metadata=metadata.ljust(32,b'\0')
                    self.wfile.write(block+b'\x02'+metadata)
            except (BrokenPipeError,ConnectionResetError):pass
            return
        if path=='/unstable.mp3':
            Handler.unstable+=1
            if Handler.unstable==1:self.send_error(503);return
            data=(root/'music/tone.mp3').read_bytes();self.send_response(200);self.send_header('Content-Type','audio/mpeg');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data);return
        if path=='/redirect':
            self.send_response(302);self.send_header('Location','/lists/radio.m3u8');self.end_headers();return
        if path.startswith('/hls/'):
            relative=Path(path.lstrip('/'))
            file=root/relative
            if '..' in relative.parts or not file.is_file():self.send_error(404);return
            data=file.read_bytes();kind='application/vnd.apple.mpegurl' if file.suffix=='.m3u8' else 'audio/aac' if file.suffix=='.aac' else 'video/mp2t'
        elif path=='/master.m3u8': data=b'#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS="mp4a.40.2"\nhls/mpegts/audio.m3u8\n';kind='application/vnd.apple.mpegurl'
        elif path=='/lists/radio.m3u8': data=b'#EXTM3U\n#EXTINF:-1,Radio local\n../audio.mp3\n';kind='audio/x-mpegurl'
        elif path=='/hls.m3u8': data=b'#EXTM3U\n#EXT-X-TARGETDURATION:2\nsegment.ts\n';kind='audio/x-mpegurl'
        elif path=='/cycle.m3u8': data=b'#EXTM3U\ncycle.m3u8\n';kind='audio/x-mpegurl'
        elif path=='/audio.mp3': data=(root/'music/tone.mp3').read_bytes();kind='audio/mpeg'
        elif path=='/too-large.m3u8': data=b'x'*(2*1024*1024+4096);kind='audio/x-mpegurl'
        else: self.send_error(404);return
        self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.end_headers()
        try:self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
env=os.environ.copy();env['TABNAMP_TEST_HTTP_BASE']=f'http://127.0.0.1:{server.server_port}'
certificate_dir=tempfile.TemporaryDirectory()
cert=Path(certificate_dir.name)
subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-keyout',str(cert/'key.pem'),'-out',str(cert/'cert.pem')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
secure=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert/'cert.pem',cert/'key.pem')
secure.socket=context.wrap_socket(secure.socket,server_side=True)
secure_thread=threading.Thread(target=secure.serve_forever,daemon=True);secure_thread.start()
env['TABNAMP_TEST_HTTPS_BASE']=f'https://127.0.0.1:{secure.server_port}'
try: result=subprocess.run([binary,'--test',str(root),artifacts],env=env)
finally: server.shutdown();server.server_close();thread.join();secure.shutdown();secure.server_close();secure_thread.join();certificate_dir.cleanup()
sys.exit(result.returncode)
