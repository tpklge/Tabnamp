# Dependências e referências

- **ESP-IDF 5.4.4**: Apache-2.0 e respectivas licenças dos componentes. SDK instalado externamente.
- **M5Stack Tab5 BSP 1.3.2, esp_lvgl_port 2.9.0, esp_codec_dev, ESP-Hosted 2.13.1, esp_wifi_remote 1.6.5, USB HID 1.1.0**: avisos/licenças nos componentes resolvidos pelo Component Manager; usualmente Apache-2.0. Versões/hashes exatos em `dependencies.lock`.
- **esp_audio_codec 2.5.0**: Espressif Modified MIT. Uso restrito a produtos Espressif; apenas firmware P4, não backend de desktop. Preserve a licença do pacote.
- **LVGL 9.3.0**: MIT. cJSON usado no host vem do ESP-IDF fixado e mantém sua licença MIT nos fontes.
- **FFmpeg 7.1.1 reduzido**: LGPL-2.1-or-later nesta configuração, sem GPL/nonfree; fonte com hash, patch, licença e receita em `third_party/ffmpeg`. Compilação estática no firmware; fornecer fontes correspondentes/objetos de relink na distribuição, não apenas o `.bin` isolado.
- **FFmpeg desktop, SDL2/SDL3 e curl**: bibliotecas externas instaladas no computador, sujeitas às respectivas licenças. FFmpeg de desktop não é transferido para o cartão como executável. SDL2-compat usa SDL3 neste ambiente.
- **Roboto Mono**: SIL Open Font License 1.1, licença em `sdcard/tabnamp/skins/classic/fonts/OFL.txt`; subconjuntos binários gerados pelo lv_font_conv. Fonte original: https://github.com/google/fonts/tree/main/ofl/robotomono. Símbolos de recuperação LVGL permanecem na fonte embutida mínima.
- **lv_font_conv 1.5.3**: ferramenta de geração, dependências fixadas no package-lock; licenças nos pacotes npm. Node baixado para `.tools` é uma ferramenta de desenvolvimento, não parte do firmware.
- **Equações dos biquads**: peaking EQ do Audio EQ Cookbook de Robert Bristow-Johnson; implementação própria.
- **Referências visuais**: as imagens originais em `references/` mantêm a atribuição em `references/FONTES.md`; não são assets do aplicativo. Marca e widgets próprios Tabnamp.
- **Fixtures ATRAC3/ATRAC3plus**: arquivos públicos de testes FFmpeg, baixados somente em build ignorado e verificados por SHA-256. Não redistribuído no firmware, no cartão de exemplo ou como música do aplicativo. Testes WMA e demais formatos usam seno original sintético.

O código próprio deste repositório é distribuído sob **GPL-3.0**, conforme `LICENSE`, já definida pelo proprietário no repositório de destino. Dependências e fontes mantêm suas próprias licenças e avisos. Dependências de câmera/vídeo trazidas transitivamente pelo BSP não representam funções de gravação ou reprodução de vídeo da aplicação.

## USB Host MSC

Espressif USB Host MSC 1.3.0, Apache-2.0. Codigo em `components/usb_host_msc`, commit de origem `8ab282ac6cde1970b660fc032b2d33a9c70e8d07` do repositorio esp-usb, caminho `host/class/msc/usb_host_msc`. Cópia local proveniente de Tab5LauncherPaludo; sem dependencia do launcher na execucao. Licenca preservada em `components/usb_host_msc/LICENCE` e incluida no pacote.
