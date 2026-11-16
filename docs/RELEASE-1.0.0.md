# Disgaea D2 Windows 1.0.0 — Stable / DirectX 12

## Português

A versão 1.0 transforma o port em um jogo de PC de verdade: depois da primeira preparação o jogo tem **menu de
opções dentro dele (F1)**, guarda suas escolhas e ganhou filtros de imagem, HDR, contorno nos personagens, capturas de
tela, avisos no estilo Steam e suporte a pacotes de texturas. O launcher também ganhou um novo **seletor de ISO em vidro**.

### O que há de novo

**Menu de opções no jogo (F1)** — tema Disgaea, abas **Vídeo**, **Filtros**, **Controles** e **Sistema**. Lembra a última
aba aberta e salva tudo em `%LOCALAPPDATA%\DisgaeaD2Recomp\settings.ini`.

- **Vídeo:** janela / tela cheia sem bordas (`F11` ou `Alt+Enter`), tamanho da janela, resolução de saída e **resolução
  interna** (vale ao reiniciar), AMD FSR 1.0, VSync e contador de FPS.
- **Filtros:** nitidez, scanlines, máscara CRT, vinheta, cores vivas, suavização (FXAA), **contorno nos personagens** e
  **HDR** (precisa de HDR ligado no Windows; reinicie ao ligar/desligar. Se o monitor não aceitar, o jogo volta sozinho para SDR).
- **Controles:** todas as teclas podem ser remapeadas, inclusive a de captura de tela.
- **Sistema:** idioma (Português/English), **pausar ao perder o foco**, salvar texturas em PNG.

**Dentro do jogo**

- **Captura de tela** (`F12`, regravável): PNG sem perdas na resolução de saída, já com upscaler e filtros, em
  `Imagens\Disgaea D2`.
- **Avisos no estilo Steam:** cartão que sobe do canto inferior direito ao salvar uma captura e quando um controle é
  conectado ou desconectado.
- **Slots de save no jogo** (salvar/carregar com confirmação) e backups no launcher.
- **Ícone** do jogo na janela, na barra de tarefas e no atalho; imagem nítida em monitores com escala de 125% / 150%
  (DPI por monitor); o `Esc` não fecha mais o jogo (use `Alt+F4` ou o botão da janela).
- **Pacote de texturas:** o jogo pode gravar cada textura em PNG e usar versões em alta que você colocar na pasta
  `texture-pack`. A ferramenta `tools/d2_texture_upscale.py` amplia as texturas (Lanczos, ou **Real-ESRGAN**, opcional e
  ainda **experimental**: o fluxo foi testado, mas não com o programa real em todos os PCs).

**Launcher**

- **Novo seletor de ISO em vidro:** janela translúcida arredondada, lista as ISOs encontradas no computador (Downloads,
  Desktop, Documentos, Games…), lembra as usadas antes, aceita setas/Enter/Esc e mantém o botão **Procurar…** para o
  explorador de arquivos. A ISO não é copiada nem alterada.

### Ficou de fora desta versão

- **Mouse como controle / cursor do mapa seguindo o mouse:** foi testado e **não entra na 1.0** (o cursor ainda ficava
  mecânico e podia sair da janela em setups com mais de um monitor). O código segue na branch `mouse-experimental` para uma versão futura.
- Vulkan, DLSS e Frame Generation continuam indisponíveis na versão estável. Mais Hz não gera quadros adicionais do jogo.

### Atualizar

1. Feche o jogo e o launcher.
2. Execute `DisgaeaD2Launcher-Setup-Windows-x64.exe`, ou extraia o ZIP portátil e abra `DisgaeaD2Launcher.exe`.
3. Clique em **Jogar**. Como o jogo foi atualizado, o launcher **recompila a build local** na primeira vez (precisa de
   internet e pode demorar). Saves e configurações são mantidos em `%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview`;
   não apague essa pasta.

O verificador de atualizações da 0.2.4 oferece esta versão na próxima consulta. Ele só abre a página da release; não baixa
nem executa o instalador sozinho.

### Primeira instalação

Forneça sua própria ISO USA compatível e o PKG da atualização **BLUS31313 US 1.40**; DLCs são opcionais e exigem seus
próprios arquivos e licenças. A primeira preparação baixa ferramentas portáteis e compila o jogo no seu computador.
ISOs cifradas ou alteradas e outras regiões não são garantidas.

### Validação e limites

O mantenedor validou em D3D12 o menu F1, o contorno, o HDR, os avisos, a pausa ao perder o foco, o ícone e a captura de
tela. Os testes automatizados cobrem o launcher, a instalação e o pipeline gráfico, não a campanha completa. O contorno
pode alcançar a HUD em telas em que ela não é detectável. Não há garantia de 60 FPS constantes nem de compatibilidade
com todo hardware. Requer Windows x64 e driver DirectX 12. O executável não possui assinatura Authenticode.

Projeto comunitário independente. Não são distribuídos jogo, atualizações, DLCs, firmware, chaves, saves nem código
recompilado do jogo. Relate problemas em
[Issues](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml) com versão, hardware,
passos e log da sessão. Confira caminhos pessoais nos logs antes de publicar e não anexe arquivos do jogo ou chaves.

## English

Version 1.0 turns the port into a proper PC game: after the first setup the game has an **in-game options menu (F1)**,
remembers your choices and gains image filters, HDR, character outlines, screenshots, Steam-style notices and texture
pack support. The launcher also gets a new **glass-style ISO picker**.

### What's new

**In-game options menu (F1)** — Disgaea theme with **Video**, **Filters**, **Controls** and **System** tabs. It remembers the
last tab and saves everything to `%LOCALAPPDATA%\DisgaeaD2Recomp\settings.ini`.

- **Video:** windowed / borderless fullscreen (`F11` or `Alt+Enter`), window size, output resolution and **internal
  resolution** (applies on restart), AMD FSR 1.0, VSync and an FPS counter.
- **Filters:** sharpness, scanlines, CRT mask, vignette, vivid colors, anti-aliasing (FXAA), **character outline** and **HDR**
  (needs HDR enabled in Windows; restart when toggling. If the display refuses it, the game falls back to SDR by itself).
- **Controls:** every key can be remapped, including the screenshot key.
- **System:** language (Português/English), **pause when unfocused**, save textures as PNG.

**In game**

- **Screenshots** (`F12`, rebindable): lossless PNG at the output resolution, with upscaler and filters applied, saved to
  `Pictures\Disgaea D2`.
- **Steam-style notices:** a card slides up from the bottom-right corner when a screenshot is saved and when a controller
  is connected or disconnected.
- **In-game save slots** (save/load with confirmation) and backups in the launcher.
- Game **icon** on the window, taskbar and shortcut; sharp image on 125% / 150% scaled monitors (per-monitor DPI); `Esc` no
  longer closes the game (use `Alt+F4` or the window button).
- **Texture pack:** the game can write every texture as PNG and load higher-resolution versions you place in the
  `texture-pack` folder. `tools/d2_texture_upscale.py` upscales textures (Lanczos, or optional **Real-ESRGAN**, still
  **experimental**: the flow was tested, but not with the real program on every PC).

**Launcher**

- **New glass-style ISO picker:** a translucent rounded window that lists the ISOs found on your computer (Downloads,
  Desktop, Documents, Games…), remembers the ones used before, supports arrows/Enter/Esc and keeps a **Browse…** button for
  the system file dialog. The ISO is never copied or modified.

### Not in this release

- **Mouse as a controller / map cursor following the mouse:** tested and **left out of 1.0** (the cursor still felt
  mechanical and could leave the window on multi-monitor setups). The code lives on the `mouse-experimental` branch for a future version.
- Vulkan, DLSS and Frame Generation remain unavailable in the stable release. Higher display refresh does not generate extra game frames.

### Updating

1. Close the game and the launcher.
2. Run `DisgaeaD2Launcher-Setup-Windows-x64.exe`, or extract the portable ZIP and open `DisgaeaD2Launcher.exe`.
3. Click **Play**. Because the game itself changed, the launcher **rebuilds the local build** the first time (needs internet
   and may take a while). Saves and settings stay in `%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview`; do not delete it.

The 0.2.4 update checker offers this version on its next check. It only opens the release page; it never downloads or
runs the installer by itself.

### First install

Provide your own supported US ISO and **BLUS31313 US 1.40** update PKG; optional DLCs require your own files and licenses.
First-run setup downloads portable tools and compiles the game on your computer. Encrypted/modified ISOs and other regions
are not guaranteed.

### Validation and limitations

The maintainer validated the F1 menu, outline, HDR, notices, pause on focus loss, icon and screenshots on D3D12.
Automated checks cover the launcher, installation and graphics pipeline, not the full campaign. The outline may reach
the HUD on screens where the HUD cannot be detected. Locked 60 FPS and every hardware configuration are not guaranteed.
Requires Windows x64 and a DirectX 12 driver. The executable is not Authenticode-signed.

Independent community project. No game data, updates/DLC, firmware, keys, saves or game-derived compiled code is
distributed. Report bugs through
[Issues](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml) with version, hardware,
reproduction steps and the session log. Review personal paths before sharing logs; do not upload game content or keys.
