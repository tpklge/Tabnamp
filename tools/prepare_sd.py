#!/usr/bin/env python3
"""Copy missing application defaults only. Never format or overwrite files."""
import argparse
from pathlib import Path
import shutil
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('mount',type=Path,help='Mounted SD root (NOT a block device)')
a=p.parse_args()
if not a.mount.is_dir(): p.error('Mount must already exist and be a directory')
source=Path(__file__).resolve().parents[1]/'sdcard'
root=a.mount.resolve()
for entry in sorted(source.rglob('*')):
    target=root/entry.relative_to(source)
    if target.is_symlink() or any(parent.is_symlink() for parent in target.parents):
        p.error('Refusing symlink: '+str(target))
    if entry.is_dir(): target.mkdir(exist_ok=True)
    elif not target.exists():
        with entry.open('rb') as src,target.open('xb') as dst: shutil.copyfileobj(src,dst)
        print('Created',target.relative_to(root))
print('Existing files preserved.')
