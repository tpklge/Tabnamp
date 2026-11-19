# Tabnamp — projeto implementado

Leia `README.md`, `docs/STATUS.md` e `docs/ARCHITECTURE.md` para o estado atual.

Compile com `./tools/build.sh build size` e empacote com `python3 tools/package_release.py`.

O binario para Launcher do Paludo/OTA e `artifacts/tabnamp-ota.bin` (identico a `build/tabnamp.bin`). O pacote entrega apenas a aplicacao, sem tabela de particoes ou bootloader. O launcher escolhe o slot. Nao use `idf.py flash` para instalar neste Tab5 com launcher.

Configuracoes e recursos ficam em `/sdcard/tabnamp`. Arquivos > SD navega o cartao; Arquivos > USB navega o pendrive FAT conectado a porta USB-A. `tools/generate_test_audio.py` produz MP3 e WAV originais para teste.

O resumo publico em `artifacts/validation.json` distingue teste fisico de emulacao. Consulte as pendencias em `docs/STATUS.md`.
