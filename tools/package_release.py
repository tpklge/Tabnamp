#!/usr/bin/env python3
"""Package the built firmware and clean microSD defaults under artifacts/."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
build = root / 'build'
out = root / 'artifacts/tabnamp-package'
firmware = out / 'firmware'
# Recreate the generated package, including licenses and metadata, each time.
if out.exists():
    shutil.rmtree(out)
firmware.mkdir(parents=True, exist_ok=True)
app = (build / 'tabnamp.bin').read_bytes()
assert len(app) > 288 and app[0] == 0xe9, 'Expected ESP application image'
assert int.from_bytes(app[12:14], 'little') == 0x12, 'Expected ESP32-P4'
assert app[23] == 1 and hashlib.sha256(app[:-32]).digest() == app[-32:], 'Image hash mismatch'
shutil.copyfile(build / 'tabnamp.bin', firmware / 'tabnamp.bin')
shutil.copyfile(build / 'tabnamp.bin', root / 'artifacts/tabnamp-ota.bin')
(firmware / 'manifest.json').write_text(json.dumps({
    'project': 'tabnamp', 'chip': 'esp32p4', 'type': 'application',
    'partition_table_required': False, 'fixed_flash_offset': None,
    'bytes': len(app), 'sha256': hashlib.sha256(app).hexdigest(),
    'install': 'Launcher do Paludo: install application in a slot large enough for the image'
}, indent=2) + '\n')

# Assemble only shipped defaults, never a simulator's mutable card or credentials.
card = out / 'microsd'
if card.exists():
    shutil.rmtree(card)
shutil.copytree(root / 'sdcard', card, ignore=shutil.ignore_patterns('.gitkeep', '.DS_Store', '*.bak', '*.tmp'))
(card / 'tabnamp/stations/stations.m3u8').write_text('#EXTM3U\n# Adicione aqui as URLs das suas radios.\n')
wifi = json.loads((card / 'tabnamp/config/wifi.json').read_text())
assert wifi['ssid'] == '' and wifi['password'] == ''
shutil.copyfile(root / 'tools/prepare_sd.py', out / 'copiar_microsd.py')
# The installer resolves defaults relative to itself.
p = out / 'copiar_microsd.py'
p.write_text(p.read_text().replace("Path(__file__).resolve().parents[1]/'sdcard'", "Path(__file__).resolve().parent/'microsd'"))
(out / 'LEIA-ME.txt').write_text('''TABNAMP — M5Stack Tab5 / ESP32-P4

FIRMWARE OTA / LAUNCHER DO PALUDO
Use firmware/tabnamp.bin (mesma imagem de artifacts/tabnamp-ota.bin).
Imagem de aplicacao ESP32-P4 com checksum e SHA-256, sem tabela de particoes,
bootloader, offset fixo ou dependencia de NVS/particoes de dados do app.
O launcher escolhe o slot. O slot precisa comportar os bytes do manifest.json.
Instale como aplicativo pelo launcher. Nao use flash/erase_flash nem grave
bootloader ou tabela de particoes para instalar ou atualizar este player.
Nao atualiza o C6. Persistencia e recursos externos ficam no microSD.

MICROSD
Copie microsd/tabnamp para a raiz de um cartao ja formatado.
Para preservar arquivos existentes:
  python3 copiar_microsd.py /Volumes/SEU_CARTAO
Coloque musicas em /tabnamp/music ou navegue por Arquivos > SD.
Pendrives FAT sao montados em /usb: use Arquivos > USB, abra um arquivo
ou use + Pasta para adicionar recursivamente. Nunca formata unidades.
Configuracoes e cache continuam em /sdcard/tabnamp.

TESTES FISICOS
Consulte docs/STATUS.md e artifacts/validation.json no projeto
para saber exatamente o que foi validado no dispositivo.
Hashes SHA-256 estao em SHA256SUMS.txt.

CODIGO-FONTE E LICENCAS
Codigo do projeto e fontes de cada release:
  https://github.com/tpklge/Tabnamp
A release inclui tabnamp-ffmpeg-source.tar.gz com o FFmpeg modificado e
configuracao usada na compilacao. As receitas estao em tools/ e third_party/.
O codigo proprio usa GPL-3.0; dependencias mantem suas licencas em licencas/.
''')
licenses = out / 'licencas'
licenses.mkdir(exist_ok=True)
shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', licenses / 'THIRD_PARTY_NOTICES.md')
shutil.copyfile(root / 'LICENSE', licenses / 'LICENSE-Tabnamp-GPL-3.0.txt')
idf = Path(os.environ.get('IDF_PATH', Path.home() / 'esp/esp-idf'))
if (idf / 'LICENSE').is_file():
    shutil.copyfile(idf / 'LICENSE', licenses / 'LICENSE-ESP-IDF.txt')
for component in sorted((root / 'managed_components').iterdir()):
    if not component.is_dir():
        continue
    for path in component.iterdir():
        if path.is_file() and ('license' in path.name.lower() or 'licence' in path.name.lower() or path.name.lower() in ('copying', 'notice')):
            target = licenses / 'components' / component.name
            target.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target / path.name)
shutil.copytree(root / 'third_party/ffmpeg', licenses / 'ffmpeg', dirs_exist_ok=True)
(licenses / 'usb_host_msc').mkdir(exist_ok=True)
for name in ('LICENCE', 'README.md'):
    shutil.copyfile(root / 'components/usb_host_msc' / name, licenses / 'usb_host_msc' / name)

for path in card.rglob('*.json'):
    json.loads(path.read_text())
for name in ('text20.bin', 'text32.bin'):
    assert (card / 'tabnamp/skins/classic/fonts' / name).stat().st_size > 0

with zipfile.ZipFile(out.parent / 'tabnamp-microsd.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(card.rglob('*')):
        if path.name == '.DS_Store':continue
        z.write(path, path.relative_to(card).as_posix() + ('/' if path.is_dir() else ''))
files = [p for p in out.rglob('*') if p.is_file() and p.name not in ('SHA256SUMS.txt', '.DS_Store')]
(out / 'SHA256SUMS.txt').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(out).as_posix()}\n' for p in sorted(files)))
with zipfile.ZipFile(out.parent / 'tabnamp-completo.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(out.rglob('*')):
        if path.name == '.DS_Store':continue
        z.write(path, path.relative_to(out).as_posix() + ('/' if path.is_dir() else ''))
for name in ('tabnamp-microsd.zip', 'tabnamp-completo.zip'):
    with zipfile.ZipFile(out.parent / name) as z:
        assert z.testzip() is None
    print(out.parent / name)
print('Application SHA-256, JSON, fonts and ZIP integrity verified; no partition table shipped.')
