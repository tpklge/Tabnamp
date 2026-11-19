# FFmpeg reduzido para codecs legados

Origem: https://github.com/FFmpeg/FFmpeg/tree/n7.1.1

Arquivo de fonte: https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.1.tar.gz

SHA-256: `f117507dc501f2a6c11f9241d8d0c3213846cfad91764361af37befd6b6c523d`.

Não se incorporou a aplicação FFmpeg inteira ao microcontrolador. `tools/build_legacy_decoders.sh` baixa a fonte, verifica hash, aplica as mudanças reproduzíveis de `tools/patch_ffmpeg.py` (também registradas integralmente em `port.patch`), configura decoders/demuxers selecionados e compila quatro bibliotecas estáticas. `tools/relocate_legacy_bss.py` move somente BSS para a seção de PSRAM do ESP-IDF. Transformações acima de 8192 pontos são rejeitadas; não deixar a API aceitar tamanhos cujas tabelas foram removidas.

Decoders: wmav1, wmav2, atrac3 e atrac3p. Demuxers: asf, oma, wav. Sem encoders, programas, vídeo, rede, asm ou threads. As fontes baixadas e modificadas ficam em `.tools/FFmpeg-n7.1.1`; não são alteradas dependências globais. A configuração completa está no script e em `.tools/ffmpeg-p4-build/config.h`/`config_components.h`/`ffbuild/config.mak`.

Licença desta configuração: **LGPL-2.1-or-later**, cópia em `LICENSE.LGPLv2.1` e avisos individuais na fonte. Preserve a licença/avisos/alterações e forneça as fontes correspondentes e material para relink ao distribuir binários estaticamente ligados. O código próprio do Tabnamp usa GPL-3.0, conforme `LICENSE` na raiz. A release inclui `tabnamp-ffmpeg-source.tar.gz`, com a árvore modificada usada no firmware e a configuração correspondente; o restante do projeto está no código-fonte da mesma tag. O projeto entrega a receita/patch e mantém objetos/arquivos `.a` no build; não tratar apenas o `.bin` como pacote de distribuição completo.

Para reconstruir/substituir a biblioteca: recrie `.tools/ffmpeg-p4-build` com os scripts, mantenha headers compatíveis e execute `tools/build.sh build`. Os objetos do app estão em `build/esp-idf/main/libmain.a`, demais componentes em `build/esp-idf`, e a linha de link em `build/CMakeFiles/tabnamp.elf.dir/link.txt`. Preserve também os scripts/linker e configurações ao empacotar objetos para relink.

O teste de host usa a mesma fonte/patch com ASan e uma ponte C++ ASan/UBSan, em executável separado do simulador. ATRAC3plus é comparado com referência PCM do fixture SND0.AT3; ver docs/CODEC_SUPPORT.md. O port rejeita OMA criptografado antes de chamar decriptação.
