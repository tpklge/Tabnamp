#!/usr/bin/env python3
"""Generate original quiet WAV/MP3 beeps and beats with ffmpeg; never overwrite."""
import argparse
import json
import math
import shutil
import struct
import subprocess
import wave
from pathlib import Path

def generate(output, seconds=12):
    if not shutil.which('ffmpeg'):
        raise RuntimeError('ffmpeg is required for MP3 encoding')
    output.mkdir(parents=True, exist_ok=True)
    result=[]
    for pattern,rate in [('apitos',44100),('batidas',48000),('estereo',44100)]:
        path=output/(pattern+'.wav')
        # -14 dBFS peak, short fades prevent clicks.
        with path.open('xb') as stream, wave.open(stream,'wb') as wav:
            wav.setparams((2,2,rate,0,'NONE','not compressed'))
            for block in range(seconds*100):
                data=bytearray()
                for i in range(rate//100):
                    t=(block*(rate//100)+i)/rate
                    phase=t%0.5
                    if pattern=='batidas':
                        value=6500*math.exp(-phase*32)*math.sin(2*math.pi*330*phase)
                    else:
                        envelope=max(0,min(1,phase/0.01,(0.20-phase)/0.01))
                        value=6500*envelope*math.sin(2*math.pi*(660 if int(t)%2 else 440)*t)
                    left=right=int(value)
                    if pattern=='estereo':
                        if int(t)%2:left=0
                        else:right=0
                    data.extend(struct.pack('<hh',left,right))
                wav.writeframesraw(data)
        mp3=path.with_suffix('.mp3')
        subprocess.run(['ffmpeg','-v','error','-n','-i',str(path),'-codec:a','libmp3lame','-b:a','128k','-metadata','title=Teste '+pattern,'-metadata','artist=Tabnamp teste sintetico',str(mp3)],check=True)
        result.append({'pattern':pattern,'rate':rate,'seconds':seconds,'wav':str(path),'mp3':str(mp3)})
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--seconds',type=int,default=12,choices=range(1,301))
    args=parser.parse_args()
    print(json.dumps(generate(args.output,args.seconds),indent=2))
