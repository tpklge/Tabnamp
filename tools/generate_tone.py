#!/usr/bin/env python3
"""Generate an original 5 second stereo PCM16/48k WAV; never overwrite."""
import math
import struct
import wave
import argparse
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path)
a=p.parse_args()
with a.output.open('xb') as stream:
    with wave.open(stream,'wb') as w:
        w.setparams((2,2,48000,0,'NONE','not compressed'))
        for block in range(500):
            data=bytearray()
            for i in range(480):
                n=block*480+i
                envelope=min(1,n/2400,(240000-n)/2400)
                v=int(3276*envelope*math.sin(2*math.pi*1000*n/48000))
                data.extend(struct.pack('<hh',v,v))
            w.writeframesraw(data)
