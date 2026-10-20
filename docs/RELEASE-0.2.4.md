# Disgaea D2 Windows — 0.2.4 Stable / DirectX 12

## Português

Esta atualização integra um **Guia de comandos** ao launcher principal, em
português e inglês. Abra a aba **Controles** ou o botão **Guia de comandos** na
tela inicial. O guia pode ser consultado enquanto o jogo está aberto e acompanha
o idioma selecionado no launcher.

O visual usa títulos encorpados com contorno escuro, dourado e painéis roxos
inspirados no jogo. As fontes são do sistema; não são distribuídas fontes
extraídas do jogo.

### Comandos atuais

O teclado funciona quando a janela do jogo tem foco e não há controle XInput
conectado à porta 0. O controle tem prioridade nessa porta.

| Tecla | Equivalente do controle |
| --- | --- |
| Setas | Direcional / analógico esquerdo |
| Z / X | Cruz / círculo (confirmar / voltar, conforme o contexto) |
| A / S | Quadrado / triângulo |
| Q / W | L1 / R1 |
| 1 / 2 | L2 / R2 |
| Enter / Tab | START / SELECT |

O guia apresenta os comandos existentes. Esta versão não acrescenta
remapeamento nem seleção direta de menus, personagens ou casas pelo ponteiro
dentro do jogo. O mouse funciona normalmente nos botões e opções do launcher.

### Atualizar

1. Feche o jogo e o launcher.
2. Execute `DisgaeaD2Launcher-Setup-Windows-x64.exe`, ou extraia o ZIP portátil
   e abra `DisgaeaD2Launcher.exe`.
3. Abra **Controles** para consultar o guia e clique em **Jogar** normalmente.

O verificador da 0.2.3 oferece esta atualização na próxima consulta. Ele abre a
página da release; não baixa nem executa o instalador automaticamente. Saves,
preferências, acesso à pasta de saves e verificação de atualizações são mantidos.
Instalador e portátil compartilham o perfil
`%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview`; não apague essa pasta.

### Primeira instalação e recursos

Forneça sua própria ISO USA compatível e o PKG da atualização **BLUS31313 US
1.40**; DLCs são opcionais e exigem seus próprios arquivos e licenças. A primeira
preparação baixa ferramentas portáteis e compila o jogo localmente, exigindo
internet e espaço em disco. Não é necessário instalar ferramentas manualmente.
ISOs cifradas ou alteradas e outras regiões não são garantidas.

Inclui DirectX 12, áudio, save/load, resolução interna e de saída, janela/tela
cheia sem bordas, VSync, seleção de Hz e FSR 1 espacial. HUD após upscaling
continua experimental e desligado por padrão. **Vulkan, DLSS, Frame Generation e
Android não estão disponíveis.** Mais Hz não gera quadros adicionais do jogo.

Para usar saves próprios, feche o jogo, faça backup e copie a pasta completa do
save para o destino de **Meus saves — abrir pasta**. Saves de PS3 precisam estar
compatíveis e descriptografados; não há conversão automática nem garantia entre
regiões/versões. Consulte também [o guia de saves](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/blob/main/docs/SAVES-PS3.md).

### Validação e limites

O runtime, o renderer e a preparação do jogo permanecem na base da 0.2.3; esta
atualização é da interface. Os testes automatizados verificam componentes e
instalação, sem executar a campanha comercial completa. O mantenedor validou
batalha, áudio, save/load, DLCs e fechamento na base estável em seu PC de teste.
Não há garantia de 60 FPS constantes ou compatibilidade com todo hardware.
Requer Windows x64 e driver DirectX 12. O executável não possui assinatura
Authenticode.

Projeto comunitário independente. Não são distribuídos jogo, atualizações, DLCs,
firmware, chaves, saves nem código recompilado do jogo. Relate problemas em
[Issues](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml)
com versão, hardware, passos e log da sessão. Confira caminhos pessoais nos logs
antes de publicar e não anexe arquivos do jogo ou chaves.

## English

This update integrates a bilingual **Command guide** into the main launcher.
Open **Controls** or the **Command guide** button on Home. The guide follows
the launcher language and remains accessible while the game runs.

Bold gold headings with dark outlines and purple panels are inspired by the
game. It uses installed system fonts, with no extracted game fonts included.

### Current controls

Keyboard fallback works when the game window has focus and no XInput controller
is connected to port 0. A controller takes priority on that port.

| Key | Controller equivalent |
| --- | --- |
| Arrow keys | D-pad / left stick |
| Z / X | Cross / circle (confirm / back, depending on context) |
| A / S | Square / triangle |
| Q / W | L1 / R1 |
| 1 / 2 | L2 / R2 |
| Enter / Tab | START / SELECT |

The guide documents existing controls. This release does not add remapping or
direct pointer selection of menus, characters or tiles inside the game. Mouse
clicks work normally on launcher buttons and options.

### Updating

1. Close the game and launcher.
2. Run `DisgaeaD2Launcher-Setup-Windows-x64.exe`, or extract the portable ZIP
   and open `DisgaeaD2Launcher.exe`.
3. Open **Controls** to read the guide, then use **Play** normally.

The 0.2.3 update checker will offer this release on its next check. It opens the
release page; it never downloads or executes an installer automatically. Saves,
settings, save-folder access and update checking are retained. Installer and
portable use `%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview`; do not delete it.

### First install and features

Provide your own supported US ISO and **BLUS31313 US 1.40** update PKG. Optional
DLCs require your own files and licenses. First-run setup downloads portable
tools and compiles the game locally, requiring internet and disk space. Manual
development-tool installation is unnecessary. Encrypted/modified ISOs and other
regions are not guaranteed.

Includes DirectX 12, audio, save/load, internal/output resolution,
windowed/borderless display, VSync, refresh-rate selection and spatial FSR 1.
HUD-after-upscale remains experimental and off by default. **Vulkan, DLSS, Frame
Generation and Android are unavailable.** Higher display refresh does not
generate extra game frames.

For your own saves, close the game, back up existing saves and copy the complete
compatible save folder into **My saves — open folder**. PS3 saves must be
compatible and decrypted; there is no automatic conversion or cross-region/version
guarantee. See the [save guide](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/blob/main/docs/SAVES-PS3.md).

### Validation and limitations

Game runtime, renderer and preparation remain on the 0.2.3 base; this is an
interface update. Automated checks cover components and installation, not the
full commercial campaign. The maintainer validated battle, audio, save/load, DLCs
and closing on the stable base on the test PC. Locked 60 FPS and every hardware
configuration are not guaranteed. Requires Windows x64 and a DirectX 12 driver.
The executable is not Authenticode-signed.

Independent community project. No game data, updates/DLC, firmware, keys, saves
or game-derived compiled code is distributed. Report bugs through
[Issues](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml)
with version, hardware, reproduction steps and the session log. Review personal
paths before sharing logs; do not upload game content or keys.
