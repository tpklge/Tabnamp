#!/usr/bin/env python3
"""Place FFmpeg's runtime tables/state in ESP-IDF's external BSS section."""
from pathlib import Path
import subprocess,re,sys
root=Path(sys.argv[1]);count=0
for folder in ('libavcodec','libavformat','libavutil','libswresample'):
    for obj in (root/folder).glob('*.o'):
        text=subprocess.check_output(['riscv32-esp-elf-readelf','-SW',str(obj)],text=True)
        sections=re.findall(r'\]\s+(\.(?:s?bss)(?:\.[^\s]+)?)\s+NOBITS',text)
        if not sections:continue
        args=['riscv32-esp-elf-objcopy']
        for name in sections:args+=['--rename-section',name+'=.ext_ram.bss'+name]
        subprocess.run(args+[str(obj)],check=True);count+=len(sections)
print('FFmpeg external BSS sections:',count)
