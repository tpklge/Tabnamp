#!/usr/bin/env python3
import sys,subprocess,shutil,json
from pathlib import Path
root=Path(sys.argv[1]).resolve()
root.mkdir(parents=True,exist_ok=True)
# Only this generated fixture directory is reset, never the user's SD tree.
for name in ('state','config','playlists','music'):
    target=root/name
    if target.exists(): shutil.rmtree(target)
source=Path(__file__).resolve().parents[1]/'sdcard/tabnamp'
shutil.copytree(source,root,dirs_exist_ok=True)
(root/'music').mkdir(exist_ok=True)
subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','sine=frequency=1000:sample_rate=48000:duration=8','-ac','2','-c:a','pcm_s16le','-y',str(root/'music/tone.wav')],check=True)
subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','anoisesrc=color=white:sample_rate=48000:duration=8:seed=72541','-ac','2','-c:a','pcm_s16le','-y',str(root/'music/noise.wav')],check=True)
for name,codec in [('tone.mp3','libmp3lame'),('tone.flac','flac'),('tone.ogg','vorbis'),('tone.opus','libopus'),('tone.aac','aac'),('tone.m4a','aac'),('tone.wma','wmav2'),('tone-wmav1.wma','wmav1')]:
    subprocess.run(['ffmpeg','-v','error','-i',str(root/'music/tone.wav'),'-metadata','title=Tom de teste','-metadata','artist=Tabnamp','-metadata','album=Álbum Teste','-metadata','album_artist=Artista Álbum','-metadata','track=3/9','-metadata','disc=1/2','-metadata','date=2026','-metadata','genre=Teste','-c:a',codec,'-strict','experimental','-y',str(root/'music'/name)],check=True)
print('Generated original fixtures:',root)

# Public FFmpeg decoder test sample, kept only in ignored build artifacts.
# It is not a music/skin resource and is never copied into sdcard/.
import urllib.request,hashlib
sample=root.parent/'atrac3-test.wav'
if not sample.exists():
    urllib.request.urlretrieve('https://samples.ffmpeg.org/A-codecs/ATRAC3/sample.ATRAC3.66kbps.44100Hz.Stereo.wav',sample)
if hashlib.sha256(sample.read_bytes()).hexdigest()!='2c6395135bc767ad3070b2d488686ad6cc30d29d2d8f9415d89684bf5df5327d':
    raise SystemExit('ATRAC3 fixture checksum mismatch')
shutil.copyfile(sample,root/'music/tone.at3')

for name in ('tone.wma','tone-wmav1.wma','tone.at3'):
    subprocess.run(['ffmpeg','-v','error','-i',str(root/'music'/name),'-ac','2','-af','asetpts=N/SR/TB','-f','f32le','-y',str(root/'music'/(name+'.f32'))],check=True)

# Original small cover image; generated as PNG with no external image dependency.
import struct,zlib
width=height=2
raw=b''.join(b'\0'+bytes([40,150,80])*width for _ in range(height))
def chunk(kind,payload):
    return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
cover=root.parent/'cover.png'
cover.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
for filename,codec in [('tone.mp3','libmp3lame'),('tone.m4a','aac'),('tone.flac','flac')]:
    subprocess.run(['ffmpeg','-v','error','-i',str(root/'music/tone.wav'),'-i',str(cover),'-map','0:a','-map','1:v','-c:a',codec,'-c:v','png','-disposition:v','attached_pic','-metadata','title=Tom de teste','-metadata','artist=Tabnamp','-metadata','album=Álbum Teste','-metadata','album_artist=Artista Álbum','-metadata','track=3/9','-metadata','disc=1/2','-metadata','date=2026','-metadata','genre=Teste','-y',str(root/'music'/filename)],check=True)
plus=root.parent/'atrac3plus-test.at3'
if not plus.exists():urllib.request.urlretrieve('https://samples.ffmpeg.org/A-codecs/ATRAC3/SND0.AT3',plus)
if hashlib.sha256(plus.read_bytes()).hexdigest()!='dda2790e790f7ec83dd1419c501bab8ec174c846a40d4487bcb098a12505ccc9':raise SystemExit('ATRAC3plus checksum mismatch')
shutil.copyfile(plus,root/'music/tone-plus.at3')
subprocess.run(['ffmpeg','-v','error','-i',str(root/'music/tone-plus.at3'),'-ac','2','-af','asetpts=N/SR/TB','-f','f32le','-y',str(root/'music/tone-plus.at3.f32')],check=True)
# HLS fixtures: one AAC audio rendition, ADTS and MPEG-TS segment profiles.
for kind,extension in [('mpegts','ts'),('adts','aac')]:
    directory=root/'hls'/kind
    directory.mkdir(parents=True,exist_ok=True)
    for stale in directory.glob('*'):stale.unlink()
    if kind=='mpegts':
        subprocess.run(['ffmpeg','-v','error','-i',str(root/'music/tone.wav'),'-c:a','aac','-f','hls','-hls_time','2','-hls_list_size','0','-hls_segment_filename',str(directory/'segment%02d.ts'),'-y',str(directory/'audio.m3u8')],check=True)
    else:
        subprocess.run(['ffmpeg','-v','error','-i',str(root/'music/tone.wav'),'-c:a','aac','-f','segment','-segment_time','2','-segment_format','adts','-y',str(directory/'segment%02d.aac')],check=True)
        names=sorted(directory.glob('segment*.aac'))
        (directory/'audio.m3u8').write_text('#EXTM3U\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:0\n'+''.join('#EXTINF:2.0,\n'+p.name+'\n' for p in names)+'#EXT-X-ENDLIST\n')
# The separate library fixture never enters the normal music folder.
library=root/'library-dataset';library.mkdir(exist_ok=True)
import wave
for i in range(5000):
    name=library/('faixa-%04d.wav'%i)
    if not name.exists():
        with wave.open(str(name),'wb') as w:w.setparams((2,2,48000,0,'NONE','not compressed'));w.writeframes(b'\0'*8)

# Continuous-radio source: no file-level ID3/Xing header between repeated audio blocks.
subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','sine=frequency=1000:sample_rate=48000:duration=60','-ac','2','-c:a','libmp3lame','-b:a','128k','-write_xing','0','-id3v2_version','0','-y',str(root/'live.mp3')],check=True)
