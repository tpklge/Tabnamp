# Desempenho e testes — 19/11/2026

Ambiente: macOS arm64, ESP-IDF 5.4.4, ESP32-P4 revisão 1.3 com 32 MiB de PSRAM, LVGL 9.3. O aparelho físico executa o firmware instalado pelo Launcher do Paludo; o simulador usa SDL e FFmpeg host. O C6 existente, versão 2.13.1, não foi regravado.

## Regressão atual de volume/interface

Aplicação `ee9de827b94b16c578d8144622d63cc4e4351092d30dcb6f2bb50cc8c80cbcf2`, 3,026,096 bytes. Build/size, 2/2 CTest, 9 testes Python e 448 verificações do emulador passaram. O display de software apresentou aproximadamente 20 fps; isso não mede o painel MIPI. A interface estática não produziu flushes adicionais, inclusive com título longo e reticências. Evidências `ui-audio-*.log` e `ui-audio-validation.json`.

O roteiro `tools/hardware_ui_audio.py` mede ambas as rotas de saída, configuração do codec/leitura I2C de DAC, MP3 maior, pausa e janela de polling/flush. Os tempos da UI representam callbacks e submissões LVGL; não medem latência física entre dedo e painel. `player perf-reset` zera os máximos antes da janela. O usuário confirmou volume adequado e resposta ao toque melhor na imagem final; é uma observação qualitativa, sem medição acústica ou de latência física.

Ensaio físico da imagem final: 30,027 s de Ghosts com tela acesa, 49,72 callbacks/s, 17,38 submissões de flush/s, atualização de UI de no máximo 4,29 ms e intervalo entre callbacks de no máximo 42 ms. Antes de agrupar as invalidações do espectro, a primeira candidata apresentou 3,69 callbacks/s e intervalo de 305 ms. Ambas as rotas de saída passaram, com registros DAC esquerdo/direito iguais a 0, PCM não nulo e zero falhas de escrita/underruns do ring. `hardware-ui-audio-report.json`, `hardware-ui-audio-first-pass.json` e `hardware-ui-audio-install.json` identificam os hashes. A rodada não repetiu a suíte completa de formatos, USB e resistência.

Os resultados completos de formatos, arquivo grande, USB, rádio e resistência a seguir ficam preservados como referência da imagem anterior. Não são reatribuídos ao hash atual.

## Referência da rodada anterior

| Verificação | Resultado / registro |
|---|---|
| Núcleo compartilhado e cancelamento durante leitura | 2/2 CTest aprovados, ASan/UBSan; `review-core-tests.log` |
| Ferramentas, preservação de SD e protocolo serial | 9 testes aprovados; `review-tools-tests.log` |
| Firmware ESP32-P4 | build/size aprovados; `review-build-final.log` |
| Integração LVGL/SDL | 444 verificações aprovadas; `.emulator-build/artifacts/report.json` |
| FPS de flush completo do display de software | 14.8817; não representa o painel MIPI |
| Ensaio de host desta rodada | 60 s locais + 60 s rádio acelerados, zero underruns do ring e 7 transições PCM |
| Mesmo port WMA v1/v2 e ATRAC3/ATRAC3plus | 1.528.832 frames comparados com referência independente; `review-legacy-tests.log` |
| Índice MP3 CBR/VBR | offsets/PTS conferidos por ffprobe; PCM independente coincide após compensar 529 amostras de priming (<1 quadro); `mp3-index-regression.json` / `mp3-pcm-regression.json` |
| M4A local com índice no fim | PCM original/visão virtual idêntico por SHA-256; `mp4-regression/` |
| Regressão determinística de seek | código anterior falha, código corrigido passa; `seek-race-regression.json` |
| Instalação final pelo launcher | 3,023,888 bytes, somente aplicação, sem alteração da tabela; `hardware-install-final.json` |
| Reprodução e controles reais | `hardware-test-report.json` |
| Codecs e MP3 do pendrive com acentos | `hardware-formats-report.json` |
| MP3 de 5,86 MB / 354,8 s e buscas grandes | `hardware-large-file-report.json` |
| Wi-Fi, ICY e HLS reais | `hardware-radio-report.json` |

SHA-256 da aplicação da rodada anterior: `bdd6da24a26c804c4e6864be1b75dcc029da6f23b33c63cbb1069b513305beab`.

O consolidado `artifacts/test-report.json` exige relatórios físicos da instalação correspondente ao hash atual. Os relatórios em andamento não contam como aprovação. Contagens da integração incluem condições e chamadas de decode, não centenas de cenários independentes. Logs de tentativas anteriores continuam no projeto como histórico; não substituem a evidência final.

## Reprodução dos ensaios

```sh
./tools/host-test.sh
python3 -m unittest discover -s tests -p test_tools.py
./tools/build.sh build size
TABNAMP_RADIO_SECONDS=60 TABNAMP_SOAK_SECONDS=60 ./tools/emulator.sh test
./tools/build_legacy_host.sh
```

`tools/test_all.sh` usa por padrão 3.600 s locais e 1.800 s de rádio **acelerados**. Essas durações históricas do host não representam ensaios físicos de uma hora e meia.

Com os fixtures no SD e o aplicativo em execução, use Python com pyserial:

```sh
python3 tools/hardware_large_file.py --port /dev/cu.usbmodem12201 --uri '/sdcard/tabnamp/music/mp3/Bruce Springsteen - Ghosts.mp3' --seconds 365 --duration-ms 354813 --max-seek-seconds 10
python3 tools/hardware_formats.py --port /dev/cu.usbmodem12201
python3 tools/hardware_radio.py --port /dev/cu.usbmodem12201 --ip IP_DESTE_COMPUTADOR --seconds 60
python3 tools/hardware_test.py --port /dev/cu.usbmodem12201 --seconds 30
```

Execute um único cliente serial por vez. Rádio usa um servidor HTTP temporário na LAN e áudio sintético original; o teste HLS usa os fixtures do emulador. O teste grande mede o tempo pelo uptime do aparelho e exige EOF dentro da janela de tempo real. Evite suspensão do computador durante a coleta.

## Ensaios prolongados e devolução do aparelho

- Local: **3603.872 s acumulados**, 120 ciclos de orientação/volume/EQ/diretórios e zero erros de escrita no codec. Variação de PSRAM de 6,364 bytes; mínimo interno amostrado de 218,159 bytes. O backlight estava automaticamente apagado durante esse ensaio.
- Houve uma interrupção de coleta de aproximadamente 140.3 s: o roteiro antigo rejeitou `Loading` durante uma repetição natural aos 59 minutos e enviou stop. O firmware não reiniciou nem registrou erro de saída. O restante foi medido em um segundo trecho. **Não é uma hora ininterrupta**. O relatório conserva os dois trechos e a observação original em `hardware-endurance-harness-interruption.json`. A regressão do roteiro agora aceita essa transição e detecta 15 s sem progresso de saída.
- Rádio: **1800.721 s**, com interrupção programada de 90 s no servidor da LAN, buffering, 4 respostas 503 e recuperação automática. Zero erros de saída; o intervalo de rádio inclui a interrupção, não representa 30 minutos ininterruptos de PCM.
- ADTS e MPEG-TS passaram. A sondagem ADTS imediatamente após o ensaio longo pegou `Loading` no corte fixo de 4 s; uma checagem separada aguardou reprodução efetiva, com limite de 30 s, e confirmou ambas as rotas no mesmo firmware. Observações originais mantidas em `hardware-radio-1800-report.json`; nova checagem em `hardware-hls-recheck-report.json`.
- Busca do MP3 maior: +60 s em 4.28 s e +180 s em 6.15 s, incluindo comandos/sondagem pela serial. Antes do índice, o salto maior levava cerca de 41 s. Não é promessa de latência para outros cartões/arquivos. Precisão anunciada de quadro, sem anúncio de gapless comprimido.
- Inventário final: Meshtastic preservado no slot 0 e uma única cópia do Tabnamp no slot 1. Removidas somente as entradas temporárias de servidor e arquivo ausente do ensaio, com backup da fila. SHA-256 do MP3 original e da cópia no SD permanece `0e7e777661ac2feb0bf48bf15e861c5c255523476954535744d0eb159ca3cd18`. O player foi deixado pausado em Ghosts, paisagem e volume 25.

## Memória e escalonamento

Seções de flash: 2.923.052 bytes; DIRAM: 120.391 bytes; BSS externa: 773.740 bytes. A janela virtual externa de 64 MiB do linker não é a capacidade física de PSRAM.

Main pré-scheduler de 8 KiB; inicialização dinâmica de 32 KiB após liberar a RAM do boot; decoder de 32 KiB em PSRAM; saída de 24 KiB interna; FFT de 18 KiB e diagnóstico de 12 KiB em PSRAM. Saída e decoder ficam no core 1; LVGL no core 0. A espera do decoder é 1 ms quando repõe áudio em reprodução e 3 ms durante busca ou com meio segundo de PCM disponível. O ajuste foi necessário porque uma espera fixa de 3 ms esvaziava o ring com frames MP3 curtos.

O dispositivo grava a cada minuto `/sdcard/tabnamp/logs/metrics.json`, com heap livre/mínimo/maior bloco interno, PSRAM e high-water marks das pilhas. Os relatórios seriais amostram memória e saída. Faixas de memória são observações da rodada, sem garantia de ausência de vazamentos para todo perfil.


Telemetria física ao fim da rodada: heap interno livre 236,555 bytes, mínimo global 206,971, maior bloco 188,416; PSRAM livre 25,432,884. Folga mínima das pilhas: decoder 8,592 bytes e saída 11,244 bytes. No port RISC-V deste SDK, `StackType_t` é `uint8_t`; os high-water marks estão em bytes. Evidência em `hardware-final-metrics.json`.

O contador pós-EQ registrou 2,238 amostras acima da unidade antes do ganho final ao longo dos diversos fixtures/configurações. Não representa uma medição de clipping analógico. Não se declara clipping zero; a saída aplica ganho e saturação e não houve falhas de escrita no codec. Underruns do ring registrados: 0; DMA/I2S continua sem instrumentação de underrun físico.

## Cobertura e limites

Os codecs Espressif rodam somente no P4; o simulador usa FFmpeg desktop. A comparação separada de legados liga o mesmo port reduzido usado no firmware. LeakSanitizer não é suportado pelo runtime macOS e permanece desativado; ASan/UBSan não constituem prova de ausência de toda falha.

Escritas aceitas pelo ES8388/I2S e PCM não nulo comprovam o caminho de saída exercitado. A confirmação acústica depende do ouvinte; zero underruns do ring não é medição de underruns DMA. As capturas em `docs/screenshots/` são renderizações do código LVGL, não fotografias do painel.

MP3 local usa índice incremental de quadros com preroll; as medições do aparelho estão no relatório do arquivo grande. Os demais codecs Espressif locais e o fallback MP3 podem exigir decodificação desde o início, consumindo dezenas de segundos para saltos grandes. Limites de formatos, medições de resistência, wake, hotplug, HID, IMU, fones e painel estão em `STATUS.md` e `CODEC_SUPPORT.md`.

## Histórico de correções físicas

Versões anteriores falharam antes de `app_main` por alocações de streams/locales C++, depois por pilha de inicialização insuficiente. Aumentar a pilha principal pré-scheduler também falhava por falta de bloco contíguo. A inicialização dinâmica após o scheduler resolveu o boot observado. O ensaio no aparelho também encontrou corrida de cancelamento no seek, remontagem automática indevida do SD, bloqueio por periféricos opcionais, reinicialização excessiva do codec e falta de tempo para IDLE1 durante buscas. Os logs antigos permanecem em `artifacts/startup-fix/`, `artifacts/stack-fix/` e `artifacts/debug-history/`; somente a rodada do hash acima fundamenta os resultados finais.
