# Estado verificável — 19/11/2026

O Tabnamp foi instalado como aplicação pelo Launcher do Paludo e executado no Tab5 conectado. As evidências de cada ensaio estão em `artifacts/` e identificam a imagem correspondente. A regressão de interface/ganho e os ensaios completos anteriores têm escopos separados.

O resumo distribuído no GitHub é [`artifacts/validation.json`](../artifacts/validation.json). Os nomes de relatórios detalhados citados abaixo identificam registros do workspace de desenvolvimento; backups da flash, logs brutos e arquivos pessoais não fazem parte da publicação.

## Teclado físico A164 — corrigido e confirmado

O teclado da base é o **Tab5 Keyboard A164 por I²C**, não um periférico USB. Faltava seu driver no Tabnamp. A revisão instalada em `app1` adiciona leitura dedicada no controlador I²C 0, GPIO0/1, endereço `0x6D`, modo HID, reconexão e entrega ao caminho existente de edição/atalhos/repetição. Enter também confirma o diálogo de texto ativo. Fonte: [documentação oficial do A164](https://docs.m5stack.com/en/tab5/Tab5_Keyboard).

Imagem: **3.052.208 bytes**, SHA-256 `0540c38555b2820ab9cbd2946a1145c36c1ac88d79044617100ee6c8a981d5bf`, disponível em `artifacts/tabnamp-ota.bin`. Build, 3/3 CTest com ASan/UBSan e 475 checks LVGL/SDL passaram. A leitura de flash confirmou a aplicação e preservação de bootloader, tabela, launcher, Meshtastic e cauda do slot.

A serial confirmou A164 presente, firmware 1, modo HID: **13 eventos recebidos, 7 pressões, 14 eventos entregues à UI e zero erros I²C** na janela capturada. O usuário confirmou: **“agora funciona”**. Evidências: `a164-boot.log`, `a164-key-monitor.json`, `a164-install/validation.json`, `a164-core-tests.log` e `a164-integration.log`. A confirmação física do teclado não constitui medição da latência do touch.

## Revisão adicional — teclado USB

Revisão HID instalada em `app1`: **3.050.032 bytes**, SHA-256 `ef3b5fd9511f5212e4b952a437bde4038dc798996827758a554819551f97d6fb`. O driver agora configura/fecha interfaces em uma tarefa separada do processamento USB, corrige a propriedade do contexto nas falhas/desconexões e inclui suporte a hubs. Build e 3/3 CTest com ASan/UBSan passaram. O teste que repõe configuração dentro do callback reproduz o bloqueio. Relatórios: `hid-deadlock-regression.json`, `hid-core-tests.log`, `hid-build.log`.

A instalação foi precedida por backup integral e seguida por leitura de verificação: somente a aplicação mudou; bootloader, tabela, launcher, Meshtastic e a cauda do slot foram preservados (`hid-install-validation.json`). A serial confirmou o driver instalado, sem erros, mas zero conexões/interfaces USB HID. `player usb` identificou somente `0781:5567`, interface classe 8/subclasse 6/protocolo 80 (armazenamento), já montada como FAT. Essa ausência é esperada para o teclado A164, que usa I²C; a correção dele é descrita acima. Evidências da revisão USB em `hid-fixed-boot.log` e `hid-runtime-initial.json`.

## Revisão anterior — digitação touch

Correções de polling do touch, redesenho por tecla, foco/edição dos campos, símbolos no teclado português, botão OK e abertura do HID estão no código e em `artifacts/tabnamp-keyboard-fix.bin`. Build ESP32-P4, 2/2 CTest do núcleo e 474 checks LVGL/SDL com ASan/UBSan passaram. O teste de 12 toques reduziu os pixels redesenhados em 82,2%; o atraso relatado de 20 s por tecla não foi reproduzido no computador. A investigação HID encontrou esta imagem instalada em `app1` e a substituiu pela revisão acima. A latência física do touch segue sem medição. Os pacotes publicados e seus relatórios permanecem identificados pela rodada anterior. Detalhes e novas métricas em `PERFORMANCE.md`.

## Última rodada física — volume e interface

Imagem de aplicação: **3,026,096 bytes**, SHA-256 `ee9de827b94b16c578d8144622d63cc4e4351092d30dcb6f2bb50cc8c80cbcf2`. `ui-audio-validation.json` reúne esta rodada; os testes físicos específicos ficam em `hardware-ui-audio-report.json`.

- Botões, linhas da playlist e teclas virtuais com raio discreto de 6 px.
- Removida a redução fixa de ganho pedida ao codec em 70: agora usa 100, mantendo o volume de software, mute, balance, rampa e saturação.
- Headroom do EQ considera a resposta combinada dos filtros com margem de 0,5 dB. Antes somava todos os ganhos positivos, reduzindo excessivamente presets com várias bandas elevadas.
- Labels e estilos só mudam quando o conteúdo muda. O cache de títulos com reticências preserva o texto original; o espectro só redesenha geometria alterada, no máximo a 20 Hz, e pausa atrás dos diálogos. A invalidação é agrupada na região do espectro para impedir que o excesso de retângulos promova um redesenho da tela inteira. Um teste com ruído branco reproduz a falha antes da correção e passa depois.
- Controles/refresh em 16 ms e leitura touch em 10 ms. A fila conserva o valor mais recente do mesmo slider sem reordenar ações intermediárias.
- Host: 2/2 CTest com ASan/UBSan, 9 testes de ferramentas e 448 verificações LVGL/SDL aprovadas, incluindo ausência de flush em UI estática, títulos longos, nível de saída e ordem de comandos. Os 60 s locais/rádio acelerados continuam sendo ensaios de host.

Atualização concluída somente no slot existente do Tabnamp pela USB serial, com backup integral e leitura posterior idêntica ao binário. O prefixo inteiro da flash (launcher, tabela e Meshtastic) e o restante do slot permaneceram idênticos ao backup; `hardware-ui-audio-install.json` registra a comparação. A imagem final iniciou pelo launcher e passou nos testes de ambas as rotas, MP3 maior e pausa. DAC esquerdo/direito em 0 dB (registros 0), zero falhas de escrita e zero underruns do ring na janela. No MP3 maior, 49,72 callbacks/s, intervalo máximo de 42 ms e duração máxima de atualização de 4,29 ms; a primeira candidata sem agrupar as áreas apresentava intervalo de 305 ms. Isso mede agendamento/submissões LVGL, não latência física do toque ou FPS do painel. O usuário confirmou volume adequado e resposta ao toque melhor. O player permaneceu reproduzindo com os ajustes atuais do usuário; essa confirmação é qualitativa, sem medição acústica ou de latência dedo/painel. Os ensaios prolongados e de codecs abaixo pertencem à imagem anterior `bdd6da24a26c804c4e6864be1b75dcc029da6f23b33c63cbb1069b513305beab`; não validam automaticamente esta imagem nova.

## Entrega e OTA

`artifacts/tabnamp-ota.bin` é a imagem de aplicação ESP32-P4 da v0.1.1, idêntica à revisão A164 confirmada no aparelho, com checksum e SHA-256 anexado verificados. Novos builds de desenvolvimento podem diferir dela. Não exige tabela específica, bootloader, offset fixo ou partições de dados próprias. O launcher escolhe um slot com espaço suficiente. Configuração, fontes, playlists e cache ficam em `/sdcard/tabnamp`; o Wi-Fi não utiliza NVS do aplicativo. Não foi atualizado o C6.

O backup integral anterior à instalação está no microSD do aparelho; seu registro é `hardware-preinstall-backup.json`. A alocação inicial foi feita pelo mecanismo do próprio launcher. As atualizações no slot reservado instalaram somente o aplicativo, sem mudar a tabela, como registrado em `hardware-install-final.json`. O Meshtastic existente foi preservado.

## Correções e integração

- Boot: redução de alocações C++ antes do scheduler e inicialização em tarefa própria; pilhas do decoder/FFT em PSRAM e saída em RAM interna. Saída criada antes de serviços opcionais. Console, HID e IMU iniciam sem bloquear áudio.
- USB: driver MSC/FAT integrado e barramento compartilhado com HID, montagem em `/usb`, botões SD/USB no navegador e caminhos UTF-8. Nunca formata unidades. Remoção libera arquivos antes da desmontagem; a lógica preserva áudio do SD.
- Áudio: codec/I2S permanece configurado durante pausa/buffering e troca de faixa com a mesma taxa. Falhas de abertura fecham o estado parcial antes de tentar novamente. Tarefas de áudio no core 1 e LVGL no core 0, com espera do decoder ajustada ao buffer.
- Busca: cancelamento durante uma leitura mantém o decoder até processar o comando; teste determinístico reproduz a falha antiga. Sondagens transitórias do SD não cancelam uma busca nem remontam o volume ativo.
- Parser comprimido: preserva pacotes parciais entre leituras, incluindo TS de 188 bytes atravessando blocos de 4096 bytes.
- M4A local com índice no fim: visão virtual de leitura reorganiza `moov` e ajusta `stco/co64` em RAM; não altera o arquivo. Índice limitado a 2 MiB. Comparação independente produziu PCM idêntico.
- Rede no boot: HTTP aguarda IP válido antes de acessar lwIP; fontes e playlists remotas usam buffering/retry enquanto o Wi-Fi inicia.
- Rádio: fim inesperado de stream LIVE agenda reconexão em vez de avançar a fila. Pausa cancela tentativas pendentes; limpar a fila não permite que um timer antigo reabra a fonte.
- Console de testes: status, reprodução por URI, controles e navegador. Transporte UTF-8 por hexadecimal contorna o filtro ASCII do terminal; cliente tolera prompt anexado e só repete consultas.
- Entrega: pacote limpo contém apenas aplicação e padrões do microSD, sem imagens mescladas, tabela, credenciais do aparelho ou músicas do usuário.

## Ensaios físicos anteriores

Os scripts registram PCM não nulo, escritas aceitas pelo codec, posição, memória e erros. Isso não é uma medição acústica ou de underruns DMA. O usuário confirmou ouvir os apitos iniciais em volume baixo; os áudios originais foram posteriormente regenerados com nível maior.

- MP3 fornecido pelo usuário, `Bruce Springsteen - Ghosts.mp3`: 5.857.123 bytes, estéreo 48 kHz, duração 354,8 s; ensaio de arquivo completo e saltos de +60/+180 s em `hardware-large-file-report.json`.
- MP3 de apitos/batidas/alternância estéreo e WAV originais, pausa, stop, seek WAV, rotações manuais, arquivo inexistente e repetição: `hardware-test-report.json`.
- FLAC, Vorbis, Opus, AAC/ADTS, M4A com índice no fim e seek, WMA v1/v2, MP3 VBR real e fixtures ATRAC3/ATRAC3plus: `hardware-formats-report.json`.
- Pendrive FAT já conectado: navegação e reprodução de MP3 com diretório acentuado, também em `hardware-formats-report.json`.
- Wi-Fi/C6, stream MP3/ICY e HLS AAC em ADTS/MPEG-TS: `hardware-radio-report.json`.

O campo `status` de cada relatório identifica se aquele ensaio passou. O consolidado só marca a suíte física completa quando os quatro relatórios pertencem à instalação atual e passaram. `focused_ui_audio_validated` identifica separadamente a regressão física de interface/ganho.

## Ensaios prolongados e devolução do aparelho

- Local: **3603.872 s acumulados**, 120 ciclos de orientação/volume/EQ/diretórios e zero erros de escrita no codec. Variação de PSRAM de 6,364 bytes; mínimo interno amostrado de 218,159 bytes. O backlight estava automaticamente apagado durante esse ensaio.
- Houve uma interrupção de coleta de aproximadamente 140.3 s: o roteiro antigo rejeitou `Loading` durante uma repetição natural aos 59 minutos e enviou stop. O firmware não reiniciou nem registrou erro de saída. O restante foi medido em um segundo trecho. **Não é uma hora ininterrupta**. O relatório conserva os dois trechos e a observação original em `hardware-endurance-harness-interruption.json`. A regressão do roteiro agora aceita essa transição e detecta 15 s sem progresso de saída.
- Rádio: **1800.721 s**, com interrupção programada de 90 s no servidor da LAN, buffering, 4 respostas 503 e recuperação automática. Zero erros de saída; o intervalo de rádio inclui a interrupção, não representa 30 minutos ininterruptos de PCM.
- ADTS e MPEG-TS passaram. A sondagem ADTS imediatamente após o ensaio longo pegou `Loading` no corte fixo de 4 s; uma checagem separada aguardou reprodução efetiva, com limite de 30 s, e confirmou ambas as rotas no mesmo firmware. Observações originais mantidas em `hardware-radio-1800-report.json`; nova checagem em `hardware-hls-recheck-report.json`.
- Busca do MP3 maior: +60 s em 4.28 s e +180 s em 6.15 s, incluindo comandos/sondagem pela serial. Antes do índice, o salto maior levava cerca de 41 s. Não é promessa de latência para outros cartões/arquivos. Precisão anunciada de quadro, sem anúncio de gapless comprimido.
- Inventário final: Meshtastic preservado no slot 0 e uma única cópia do Tabnamp no slot 1. Removidas somente as entradas temporárias de servidor e arquivo ausente do ensaio, com backup da fila. SHA-256 do MP3 original e da cópia no SD permanece `0e7e777661ac2feb0bf48bf15e861c5c255523476954535744d0eb159ca3cd18`. O player foi deixado pausado em Ghosts, paisagem e volume 25.

## Validação de host

Dois executáveis CTest com ASan/UBSan, nove testes Python de ferramentas e integração LVGL/SDL. O port WMA/ATRAC compara 1.528.832 frames com uma referência independente. A integração exercita também cancelamento, fontes SD/USB, retirada lógica de USB preservando SD, orientação, playlists, DSP, teclado, persistência, HLS e biblioteca de 5.000 entradas. Os resultados de duração acelerada não são ensaios físicos de resistência.

## Limites de suporte e medições restantes

MP3 local CBR/VBR tem índice incremental de quadros e preroll; fontes incomuns/free-format mantêm fallback sequencial. Os demais codecs Espressif locais decodificam desde o início e podem demorar dezenas de segundos em saltos grandes. As buscas permanecem canceláveis. M4A remoto precisa de índice antes dos dados. HLS aceita AAC/ADTS e MPEG-TS; DRM, fMP4 e byte ranges são rejeitados. WMA Pro/Voice/Lossless e ATRAC1/Advanced Lossless não estão integrados. Os fixtures não comprovam toda variante de codec. Não se anuncia gapless comprimido sem comparação de trimming. Fones têm seleção manual; detecção do jack não foi comprovada pelo BSP.

Permanecem pendentes uma hora ininterrupta no perfil com tela acesa/estresse de navegação touch, dez ciclos de wake com latência ≤500 ms, medição de fps do painel/DMA, teclado HID real, eixos/rotação automática da IMU, retirada/reinserção física de SD/USB, cartão cheio, queda de energia e níveis nos fones. Os testes lógicos e as rotações manuais não encerram esses itens. Não há edição de tags.
