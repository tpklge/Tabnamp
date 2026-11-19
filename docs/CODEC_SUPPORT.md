# Matriz de codecs e evidência

Em 19/11/2026, o Tab5 executou WAV, MP3 CBR/VBR, FLAC, Vorbis, Opus, AAC/ADTS, AAC/M4A, WMA v1/v2, ATRAC3 e ATRAC3plus, com PCM nao nulo e escritas aceitas pelo ES8388/I2S. Foram exercitados arquivos sinteticos e fixtures especificos: isso nao valida todos os bitrates, contêineres e variantes de cada familia. Evidencias em `artifacts/hardware-formats-report.json` e `artifacts/hardware-test-report.json`.

Assinatura e cabecalhos orientam a selecao; extensao isolada nao comprova compatibilidade.

| Codec/perfil | Contêiner | Implementação no firmware | Compila P4 | Teste no host | Limites atuais |
|---|---|---|---|---|---|
| PCM signed 16 LE | RIFF/WAVE | Parser/decoder próprio | Sim | Mesma implementação, fixtures/truncamentos/seek | Mono/estéreo 44,1/48 kHz; sem RF64/extensible/ADPCM |
| MPEG Layer III | MP3 | esp_audio_codec 2.5.0 | Sim | Backend desktop MP3 + metadados próprios | CBR e VBR exercitados no P4; busca local por indice incremental de quadros e preroll; free-format/fontes incomuns mantem fallback sequencial; trimming/gapless nao anunciados |
| FLAC | FLAC | esp_audio_codec 2.5.0 | Sim | Backend desktop + tags próprias | Perfil inicial 16-bit, 44,1/48 kHz; limites superiores não anunciados |
| Vorbis | Ogg | Parser Ogg + decoder Espressif | Sim | Fixture Vorbis sintético desktop | Comentários reconstruídos entre páginas e duração por granule; fixture reproduzido no P4; trimming exato permanece pendente |
| Opus | Ogg | Parser Ogg + decoder Espressif | Sim | Fixture Opus sintético desktop | Pre-skip/ganho/trimming dependem do parser e precisam de comparação no P4 |
| AAC-LC | ADTS | esp_audio_codec 2.5.0 | Sim | Fixture sintético desktop | HE-AAC não anunciado; DRM sem suporte |
| AAC-LC | M4A | Parser M4A Espressif | Sim | Fixture M4A sintético desktop | Indice moov no inicio ou no fim; visao somente de leitura com stco/co64 ajustados em RAM, limite de indice 2 MiB; reproducao e seek exercitados no P4 |
| WMA Standard v1/v2 | ASF | FFmpeg 7.1.1 reduzido/portado | Sim, ligado no firmware | **Mesmo port** no host; PCM comparado com FFmpeg independente | Arquivos locais; sem WMA Pro/Voice/Lossless ou DRM; reproducao do fixture em tempo real exercitada no P4 |
| ATRAC3 | RIFF/WAVE compatível, OMA via demuxer | FFmpeg 7.1.1 reduzido/portado | Sim, ligado no firmware | **Mesmo port**, fixture ATRAC3 66 kbps / 44,1 kHz estéreo; PCM comparado | RIFF exercitado no host e no P4; OMA/AA3 ainda precisam de fixture; arquivos locais |
| ATRAC3plus | OMA/RIFF compatível | Decoder `atrac3p` selecionado | Sim | **Mesmo port**, fixture RIFF SND0.AT3; PCM comparado | RIFF exercitado no host e no P4; OMA/AA3 ainda precisam de fixture; arquivos locais |
| ATRAC1 / Advanced Lossless | Variantes Sony | Não integrado | Não | Não | Pendente |
| WMA Pro / Voice / Lossless | ASF | Não integrado | Não | Não | Pendente |

O backend desktop do simulador usa FFmpeg instalado; os testes comuns não executam os binários Espressif. Há um segundo executável, `legacy_tests`, ligado exclusivamente ao FFmpeg 7.1.1 **com os mesmos patches do port**, evitando confundir suporte de desktop genérico com o código preparado para P4.

## Comparação dos legados

Amostras WMA v1/v2 geradas a partir de seno original 1 kHz, estéreo 48 kHz, oito segundos. Referência PCM f32 estéreo produzida pelo FFmpeg independente instalado; versão de teste do port sem asm. Resultado observado:

| Perfil | Frames comparados | Erro máximo absoluto PCM normalizado | SNR |
|---|---:|---:|---:|
| WMA v2 | 382.976 | 2,98023×10⁻⁸ | 140,328 dB |
| WMA v1 | 382.976 | 3,72529×10⁻⁸ | 141,228 dB |
| ATRAC3 | 195.584 | 0,00116429 | 74,7615 dB |
| ATRAC3plus | 567.296 | 1,49012×10⁻⁷ | 140,472 dB |

ATRAC3 compara versões/implementações de FFT diferentes, portanto o teste não exige identidade bit a bit: limite de erro máximo 0,002 e SNR >50 dB. Os valores reais são registrados, sem ocultar a diferença. O ensaio no P4 confirma reproducao dos fixtures em tempo real; nao substitui uma varredura de desempenho e consumo por bitrate/perfil.

Fixture público de decodificação: [repositório de amostras FFmpeg](https://samples.ffmpeg.org/A-codecs/ATRAC3/sample.ATRAC3.66kbps.44100Hz.Stereo.wav), SHA-256 `2c6395135bc767ad3070b2d488686ad6cc30d29d2d8f9415d89684bf5df5327d`. Fixture de desenvolvimento, copiado ao SD do aparelho somente para o ensaio; nao integra o firmware nem o pacote SD de distribuicao. Não foi usada renomeação de extensão como prova de codec: ffprobe identifica ATRAC3 no fixture.

## Licenças

- Espressif áudio: licença Espressif Modified MIT, restrita a produtos Espressif; componente nunca é executado no host.
- FFmpeg 7.1.1 nesta configuração: LGPL-2.1-or-later, sem habilitar GPL/nonfree. Cópia da licença, patch, SHA-256, receita e instruções de relink em `third_party/ffmpeg`.
- BSP/esp_codec_dev: Apache-2.0; LVGL: MIT. Ver `THIRD_PARTY_NOTICES.md`.

Conversão externa não é requisito da reprodução local e não é apresentada como decoder nativo. Arquivos protegidos ou perfis indisponíveis devem falhar no decoder, sem tentativa de remover DRM. Há preflight de DRM para ASF, MP4 e OMA; a chamada de decriptação do demuxer OMA foi desabilitada no port.

Fixture ATRAC3plus: https://samples.ffmpeg.org/A-codecs/ATRAC3/SND0.AT3, SHA-256 `dda2790e790f7ec83dd1419c501bab8ec174c846a40d4487bcb098a12505ccc9`. Mantido no build de testes e no SD do aparelho de ensaio; excluido do pacote de distribuicao. Referência PCM usa timestamps reconstruídos a partir do número de amostras para evitar descontinuidades do fixture.

## M4A local e por rede

O parser Espressif 2.5 rejeita `mdat` antes de `moov`. Para arquivos locais, `core/mp4_stream.*` cria uma visao virtual fast-start, movendo apenas o indice em RAM e corrigindo offsets `stco`/`co64`. O audio e lido do arquivo original, sem conversao ou regravacao. Testes com sanitizers cobrem limites, offsets e rewind; FFmpeg independente decodificou o original e a visao com PCM SHA-256 identico. O aparelho reproduziu o M4A originalmente rejeitado e executou seek.

M4A por HTTP continua exigindo indice antes dos dados (fast-start); a visao com seek e exclusiva de fontes locais. fMP4/DRM continuam fora do perfil. A busca dos codecs Espressif e cancelavel e pode decodificar desde o inicio; saltos distantes demoram mais.
