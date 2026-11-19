#!/usr/bin/env python3
"""Apply the documented newlib and bounded-transform FFmpeg port changes."""
from pathlib import Path
import re,sys
root=Path(sys.argv[1])
p=root/'libavformat/wavdec.c';p.write_text(p.read_text().replace('    unsigned int tag;','    uint32_t tag;'))
p=root/'libavutil/tx_template.c'
lines=[]
for line in p.read_text().splitlines(True):
    match=re.search(r'(?:SR_TABLE\(|DECL_SR_CODELET\(|ff_tx_fft)([0-9]+)',line)
    if match and int(match[1])>8192: continue
    lines.append(line)
p.write_text(''.join(lines))
p=root/'libavutil/tx.c';p.write_text(p.read_text().replace('if (!len || type >= AV_TX_NB || !ctx || !tx)','if (!len || len > 8192 || type >= AV_TX_NB || !ctx || !tx)'))

# Application policy: never invoke OMA's built-in decryption path.
p=root/'libavformat/omadec.c'
p.write_text(p.read_text().replace('if (eid != -1 && eid != -128 && decrypt_init(s, extra_meta, buf) < 0)', 'if (eid != -1 && eid != -128)'))
