# Referências do Tabnamp

Pesquisa realizada em 18/11/2026. URLs de branches e documentação podem mudar; registrar commits e versões na implementação.

## Referências visuais externas

### 01_webamp_classic.png

- Origem primária: README do projeto Webamp, uma reimplementação do Winamp 2 para navegador.
- Página: https://github.com/captbaritone/webamp
- Imagem: https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp-demo/images/preview.png
- Dimensões originais: 1474×966.
- Uso no projeto: referência visual dos painéis do player, EQ e playlist. Ignorar navegador e fundo. Não é código de firmware nem mockup do Tabnamp.

Consulte a imagem na origem indicada acima.

### 02_winamp_classic.jpg

- Origem complementar: artigo de La República sobre o Winamp.
- Página: https://larepublica.pe/tecnologia/2022/04/21/que-paso-con-winamp-el-famoso-reproductor-de-musica-de-los-2000
- Imagem: https://imgmedia.larepublica.pe/1200x735/larepublica/original/2022/04/21/62619e1639f0486174285681.jpg
- Dimensões originais: 1200×735.
- Uso no projeto: referência visual dos controles, cores e organização. A imagem não é uma skin reutilizável nem comprova licença de recursos visuais.

Consulte a imagem na origem indicada acima.

As imagens são referências de design com atribuição, não recursos para incorporar diretamente ao firmware ou uma declaração de que marcas/skins de terceiros estão sob a licença do aplicativo. Criar recursos próprios do Tabnamp.

## Documentação técnica primária

| Fonte | Finalidade |
|---|---|
| https://docs.m5stack.com/en/core/Tab5 | Hardware, revisões, áudio, microSD, USB e conectividade |
| https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_tab5 | BSP do Tab5 e integração de periféricos; conferir revisão suportada |
| https://github.com/espressif/esp-adf-libs/tree/master/esp_audio_codec | Decodificadores e matriz de compatibilidade com ESP32-P4 |
| https://github.com/espressif/esp-adf | Exemplos e componentes de áudio; confirmar suporte ao ambiente escolhido |
| https://www.ffmpeg.org/doxygen/trunk/atrac3_8c.html | Decoder ATRAC3, contêineres e dependências; não representa port pronto para ESP32 |
| https://github.com/Rockbox/rockbox | Candidato para pesquisa de decoders portáveis e licenças; compatibilidade com Tab5 não comprovada |
| https://lvgl.io/docs/open/widgets/image | Imagens de arquivos e recursos externos no LVGL |
| https://lvgl.io/docs/open/main-modules/fonts/binfont_loader | Fontes binárias externas; usar documentação da versão fixada |
| https://lvgl.io/docs/open/main-modules/display/setup | Integração do display, buffers e flush |
| https://github.com/captbaritone/webamp | Referência de interação e aparência; aplicação web não é base de execução para Tab5 |

## Constatações e decisões de projeto

- O Tab5 possui recursos de áudio e saída para fones; a documentação e o BSP devem orientar os drivers da revisão utilizada.
- O `esp_audio_codec` apresenta suporte ao ESP32-P4 e diversos codecs de áudio; WMA e ATRAC não aparecem na lista consultada. Isso exige avaliação de outra implementação, não comprova impossibilidade no hardware.
- FFmpeg possui decoder ATRAC3; essa existência não garante viabilidade de port para P4 nem suporte a toda a família ATRAC.
- Separar codec, perfil e contêiner na matriz de suporte. M3U descreve uma playlist; não é um codec. M3U8 pode ser uma playlist UTF-8 comum ou um manifesto HLS.
- Arquitetura de buffers, tamanhos, faixas de EQ, disposição da UI e metas de teste no prompt são propostas para este projeto; precisam de medição e ajuste no hardware.
