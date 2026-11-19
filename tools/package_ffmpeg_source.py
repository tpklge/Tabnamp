#!/usr/bin/env python3
"""Bundle the modified FFmpeg source and configuration used by the firmware."""
from pathlib import Path
import gzip
import hashlib
import io
import json
import tarfile

root = Path(__file__).resolve().parents[1]
source = root / '.tools/FFmpeg-n7.1.1'
build = root / '.tools/ffmpeg-p4-build'
assert (source / 'configure').is_file(), 'Build the legacy decoders first'
destination = root / 'artifacts/tabnamp-ffmpeg-source.tar.gz'
app_sha = hashlib.sha256((root / 'build/tabnamp.bin').read_bytes()).hexdigest()


def metadata(info):
    if any(part in ('.DS_Store', '.git', '__pycache__') for part in Path(info.name).parts):
        return None
    info.uid = info.gid = 0
    info.uname = info.gname = ''
    info.mtime = 0
    return info


with destination.open('wb') as output, gzip.GzipFile(fileobj=output, mode='wb', mtime=0) as compressed, tarfile.open(fileobj=compressed, mode='w|') as archive:
    archive.add(source, arcname='tabnamp-ffmpeg-source/FFmpeg-n7.1.1', filter=metadata)
    for name in ('config.h', 'config_components.h', 'ffbuild/config.mak'):
        data = (build / name).read_text().replace(str(root), '${TABNAMP_ROOT}').encode()
        info = tarfile.TarInfo('tabnamp-ffmpeg-source/build-config/' + name)
        info.size = len(data)
        archive.addfile(info, io.BytesIO(data))
    for name in ('tools/build_legacy_decoders.sh', 'tools/patch_ffmpeg.py', 'tools/relocate_legacy_bss.py', 'third_party/ffmpeg/port.patch', 'third_party/ffmpeg/LICENSE.LGPLv2.1', 'LICENSE', 'sdkconfig', 'sdkconfig.defaults', 'dependencies.lock'):
        archive.add(root / name, arcname='tabnamp-ffmpeg-source/project/' + name, filter=metadata)
    data = json.dumps({'project': 'Tabnamp', 'firmware_sha256': app_sha, 'ffmpeg': 'n7.1.1',
        'upstream_archive_sha256': 'f117507dc501f2a6c11f9241d8d0c3213846cfad91764361af37befd6b6c523d',
        'source': 'FFmpeg-n7.1.1 contains the modified source used by the build.',
        'configuration': 'build-config records the actual configuration; TABNAMP_ROOT replaces the local workspace path.',
        'rebuild': 'Use the Tabnamp source from the same release tag and its tools/build.sh with ESP-IDF 5.4.4. See third_party/ffmpeg/README.md for rebuilding and relinking.'}, indent=2).encode() + b'\n'
    info = tarfile.TarInfo('tabnamp-ffmpeg-source/manifest.json')
    info.size = len(data)
    archive.addfile(info, io.BytesIO(data))
print(destination)
