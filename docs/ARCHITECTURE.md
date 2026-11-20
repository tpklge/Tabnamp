# Arquitetura integrada

## Código compartilhado

- `core/mp4_stream.*`: visao local M4A somente de leitura; encontra `moov` no fim, fornece indice antes de `mdat` e ajusta `stco`/`co64` em RAM (indice limitado a 2 MiB). Nao reescreve nem converte a musica. Verifica limites/profundidade e cancelamento.
- `core/media.*`: seleção por assinatura, WAV PCM16 compartilhado, wrapper HLS e preflight de DRM. ASF/OMA/RIFF não PCM usam o backend legado no dispositivo.
- `core/engine.*`: estados, comandos limitados, fila/histórico por URI, geração/cancelamento, prebuffer contado em frames, prefetch PCM, persistência e telemetria.
- `core/playlist.*` e `core/hls.*`: playlists limitadas, resolução relativa e redirecionamentos, cinco níveis/ciclos; HLS AAC/ADTS ou AAC MPEG-TS, variantes, sequências, reload ao vivo. Rejeita criptografia, fMP4 e byte ranges.
- `core/library.*`: indexação incremental, um passo de diretório e uma leitura de tags por vez; cache schema 2 limitado a 10.000 slots, validado por caminho/tamanho/mtime. Colisões reconstroem o slot. Até 10.000 faixas, 1.024 diretórios e profundidade 24, sem seguir symlinks.
- `core/storage.*`: JSON estrito/limitado, mutex recursivo para filesystem compartilhado, backup e substituição. Sessão schema 2 junta fila/tags básicas/posição; recuperação valida o conjunto antes de aceitar `.bak`.
- `core/metadata.*`: ID3v1/v2, UTF-8/16/Latin-1, FLAC, Ogg com reconstrução entre páginas, atoms M4A e ASF. Duração/propriedades e cache de capas PNG/JPEG: 512 KiB por imagem, 1024×1024, 64 arquivos/16 MiB. Não edita músicas.
- `core/eq.*`: dez peaking biquads RBJ, Q=1,4, por canal; ±12 dB, bandas ≥45% da taxa desativadas, headroom calculado pela resposta combinada dos filtros (193 pontos logarítmicos e centros das bandas, margem de 0,5 dB), crossfade de parâmetros por bloco e resposta real usada na curva gráfica.
- `core/output.hpp`: rampa, balance, mute, saturação finita, estéreo para fones ou downmix mono para alto-falante. Usado pelo host e firmware.
- `core/spectrum.*`: FFT 1024/Hann, 24 bandas, pós-EQ/pré-volume; captura mais recente, sem backlog.
- `core/hid.*`, `core/orientation.hpp`, `core/screen.hpp`: descritores de teclado/Consumer limitados, debounce de gravidade e estado de energia com supressão integral do gesto/acorde de wake.
- `ui/ui.*`: a mesma UI LVGL 9.3 nos dois destinos; linhas virtualizadas, diálogos, seleção/arraste, detalhes/capas, EQ, preferências e layouts 1280×720/720×1280. Animações/invalidação e FFT param com tela apagada.

## Concorrência e memória

O main do SDK usa 8 KiB para caber no heap anterior ao scheduler. Dentro de `app_main`, a RAM de boot já foi liberada pelo ESP-IDF; nesse momento cria-se `tabnamp_init` com 32 KiB para filesystem/UI/serviços. A tarefa se exclui após a inicialização. Não aumentar a pilha pre-scheduler para acomodar a UI.

Decoder/controlador: prioridade 5, core 1, stack 32 KiB em PSRAM, com pausa cooperativa de 1 ms ao repor PCM durante reprodução e 3 ms durante busca ou quando há pelo menos meio segundo no buffer. Saida: prioridade 6, core 1, stack interna de 24 KiB, pacing por I2S. LVGL fica no core 0. FFT: prioridade 2, stack 18 KiB em PSRAM. Reader comprimido: prioridade 4, stack 16 KiB, ring de 1 MiB; IO HTTP pode ser cancelado via handle registrado. LVGL usa lock próprio, timer de controles e refresh de 16 ms, leitura touch de 10 ms e espectro limitado a 50 ms; USB enfileira edges e reconcilia estado se houver overflow. `platform/tab5/a164.cpp` lê o teclado físico oficial em tarefa de prioridade 3/stack 4 KiB, com I²C dedicado no controlador 0 (GPIO0/1, 100 kHz, endereço 0x6D), separado do BSP no controlador 1. Configura modo HID e drena até 32 eventos por ciclo de 10 ms; nenhuma transação I²C do teclado roda no LVGL. Ausência/erro solta as teclas e tenta reconectar. IMU publica orientação por atômicos. Rede e telemetria têm workers de baixa prioridade.

PCM: 256×512 frames estéreo float (~1 MiB), 2,73 s a 48 kHz quando cheio. Como cada bloco pode ser parcial, o prebuffer usa soma exata de frames: local 4096, rádio dois segundos. Pré-abertura de próximo WAV PCM compatível prepara oito blocos; troca preserva DSP e geração, evitando reabrir a saída. Não se anuncia trimming/gapless de codecs comprimidos.

Troca/seek/stop incrementam época de cancelamento e geração; blocos lidos após cancelamento são descartados. O consumidor confirma frames aceitos, e acknowledgements antigos não avançam o relógio. Latência e descarte de DMA físico precisam de medição. Indexação só avança quando parado ou com reserva PCM suficiente; navegador avança uma entrada por passo. Serialização de sessão e ordenação ocorrem fora do mutex da saída.

PSRAM atende grandes buffers e BSS legado. Heap interno reserva 192 KiB e encaminha alocações acima de 32 bytes para a política externa do ESP-IDF. Drivers mantêm seus requisitos de DMA. Telemetria a cada 60 s grava somente snapshot limitado com backup, incluindo heap mínimo/maior bloco e stacks; os valores precisam ser coletados no aparelho.

## Backends

Tab5: BSP 1.3.2 detecta painel/touch, ES8388 via esp_codec_dev, SDMMC slot 0 e ESP-Hosted 2.13.1/esp_wifi_remote 1.6.5 no C6 SDIO slot 1. IMU via sensor hub do BSP. USB host compartilhado, iniciado uma unica vez pelo BSP; MSC 1.3.0 vendorizado monta pendrives FAT em `/usb` sem formatar. A desconexao cancela a leitura USB, fecha decoder/prefetch/indexador/navegador e desmonta na tarefa controladora antes do proximo comando de arquivo; reproducao de SD e preservada. USB HID 1.1.0 com boot ou descritores report/NKRO/Consumer, US/ABNT2 e Caps Lock. Remontagem SD é serializada; não formata. Rotas de saída são manuais, sem alegação de jack detect.

Codecs comuns P4: esp_audio_codec 2.5.0; a versão 2.6 exige revisão P4 ≥3 e não é utilizada. Legados: FFmpeg 7.1.1 reduzido wmav1/wmav2/atrac3/atrac3p, demuxers ASF/OMA/WAV, somente file, sem programas/vídeo/rede/encoders/asm/threads. Patches em `third_party/ffmpeg/port.patch`: tag WAV uint32_t, transforms limitados a 8192, BSS externa por script e rejeição de OMA criptografado antes de decriptação.

Host: SDL/display software e FFmpeg desktop para formatos comuns; ponte legado usa separadamente o mesmo FFmpeg reduzido/alterado com ASan. Não executa drivers nem binários proprietários Espressif.

## Persistência

Raiz `/sdcard/tabnamp`, sem NVS do app. `config/settings.json` guarda preferências; `config/ui.json` carrega orientação/brilho e é atualizado junto. Keymap, presets/preamp e credenciais Wi-Fi têm arquivos próprios; senhas não são registradas. Configurações têm debounce de 2 s; sessão checkpoint a cada 15 s, retorno pausado e volume limitado a 40%.

Writes usam temporário, flush/fsync, fechamento, backup e rename. Checkpoint único elimina inconsistência entre fila e posição de arquivos diferentes; FAT ainda exige testes de queda física de energia. Cache de tags/capas é reconstruível e limitado. Fontes Roboto Mono binfont e paleta vêm do SD; fallback mínimo está na flash. Nenhuma imagem de referência é incorporada ao firmware.

## Distribuicao e diagnostico USB

O artefato de entrega e a imagem de aplicacao `artifacts/tabnamp-ota.bin`, identica a `build/tabnamp.bin`. O empacotamento verifica chip e SHA-256 anexado e entrega somente o app: nao inclui bootloader, imagem mesclada ou tabela. A tabela local em `partitions.csv` serve ao build do SDK, sem fixar o slot de execucao no launcher. Nao ha NVS, SPIFFS ou outra particao de dados obrigatoria do aplicativo. O slot escolhido pelo launcher precisa comportar o tamanho do app.

`tools/launcher_install.py` usa serial para obter a sessao do launcher e sua API local autenticada para transferir e instalar. Por padrao recusa reorganizacao da flash; com `--allow-launcher-allocation`, permite que o proprio launcher ajuste os slots pelo seu mecanismo de recuperacao, apos backup integral separado. Registra os apps antes/depois e compara o layout. Nao usa esptool. `tools/player_serial.py` controla a aplicacao em execucao: status, reproducao por URI, pause, stop, seek, volume, EQ, orientacao e reboot. A telemetria distingue escrita aceita pelo codec de confirmacao acustica; contadores I2S nao medem o som no ambiente.

O filesystem FAT usa UTF-8 nos caminhos (SD e USB), necessario para nomes com acentos. A saida conserva o codec/I2S configurado durante pausa, buffering e troca de faixas com a mesma taxa; o DMA auto-clear produz silencio sem reiniciar o ES8388. Ao mudar a taxa, falhas de abertura fecham o estado parcial e permitem ate tres tentativas.

O sink e criado antes de USB/Wi-Fi. HID e console inicializam em workers separados; a IMU inicia de forma assincrona quando o modo automatico e solicitado. Assim a disponibilidade de um periferico opcional nao impede a criacao do controlador e da saida.

Cancelamento de leitura preserva o decoder e o relogio ate o comando enfileirado assumir a operacao. Isso permite seek mesmo quando o comando chega durante uma leitura; stop/troca liberam a fonte no respectivo handler. Um teste deterministico injeta o comando dentro de `read`: reproduz a falha antiga e passa com a correcao.

A recuperacao automatica do SD exige tres falhas persistentes e estado inativo/erro. EBUSY/EMFILE/timeouts transitorios nao provocam remontagem; comandos automaticos sao ignorados se uma reproducao/busca comecou antes do processamento. O leitor ativo nunca perde seu volume por uma sondagem de diagnostico.

O cliente serial aceita o JSON antes de um prompt anexado, distingue respostas de consulta e de comando e repete somente consultas. Argumentos UTF-8 sao enviados em hexadecimal para contornar o filtro ASCII do linenoise; a UI usa diretamente os caminhos UTF-8 do FatFS.

O fim de um stream LIVE agenda reconexao com backoff, sem avancar para outra faixa. Pausa cancela tentativas pendentes; filas limpas ou fontes paradas nao podem ser reabertas por um timer antigo. Testes deterministas exercitam EOF remoto, retomada, pausa e limpeza. O console usa `band INDICE GANHO_DB` e publica o estado real do EQ para os ensaios de resistencia.

`core/mp3_seek.*` constrói um índice incremental a cada 64 quadros Layer III, lendo blocos de 32 KiB, sem usar proporção de bytes/bitrate. Trata MPEG-1/2/2.5, CBR/VBR, ID3/footer e metadados Xing/Info/VBRI; considera o encoder delay LAME quando disponível. Cada busca inclui ao menos um segundo de preroll para reconstruir reservatório/filtros. O worker comprimido é parado antes de reposicionar o FILE. Índice limitado a 65.536 pontos; leituras cooperativas e canceláveis. Fontes incomuns/free-format usam o seek sequencial anterior. A precisão anunciada é de quadro, sem promessa de gapless comprimido.

O adaptador Espressif repete ate 16 vezes os mesmos bytes quando o parser avanca apenas seu estado interno, com pausa cooperativa de 1 ms. Depois conserva um prefixo comprimido nao consumido e acrescenta bytes antes de tentar novamente. Pacotes TS de 188 bytes podem atravessar os blocos de IO de 4096 bytes; isso nao significa corrupcao. Sem progresso com o buffer inteiro ainda produz erro limitado.

HTTP so abre clientes apos o evento de IP valido, sinalizado por atomico. Antes disso, radio e playlist remota ficam em buffering/retry; nao chamam lwIP antes de existir seu mailbox. A criacao do worker Wi-Fi e serializada e as tentativas de reconexao sao atomicas.

A interface compara o texto original antes de alterar labels, inclusive labels com reticências, e só altera cores/geometria quando necessário. O espectro não é redesenhado por polling nem por trás de diálogos. Uma única invalidação de 570×50 px contém todas as barras e picos antes de alterar os objetos; isso impede o overflow da lista de áreas sujas do LVGL que promovia o espectro a redesenho integral. Valores consecutivos do mesmo slider pendentes na fila são substituídos pelo mais recente, sem atravessar outros comandos ou bandas.

O ganho do codec é configurado em 100 (referência nominal de 0 dB), removendo a redução fixa adicional anteriormente solicitada em 70. Volume, balance e mute continuam no ganho de software com rampa e saturação. O status serial publica o volume configurado, leitura I2C dos dois registros DAC, máximos de tempo da atualização/polling da UI e contagem de submissões de flush. `player perf-reset` reinicia os máximos para uma janela de medição; esses contadores não medem FPS do painel nem latência física do toque.
