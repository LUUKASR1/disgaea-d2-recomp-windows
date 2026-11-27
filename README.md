<p align="center"><img alt="Disgaea D2 Win + Android Recomp" src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/6e124d3ac0a64a9a0bb720d54ff1793993027659/docs/images/banner-win-android.png" width="720"></p>

<h1 align="center">Disgaea D2: A Brighter Darkness — Recomp</h1>
<p align="center"><b>Windows e Android</b> · jogue o seu próprio disco de PS3, recompilado para rodar nativamente.<br>
<a href="#-português">Português</a> · <a href="#-english">English</a></p>

<p align="center">
<a href="https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/latest"><img alt="Última versão" src="https://img.shields.io/github/v/release/LUUKASR1/disgaea-d2-recomp-windows?style=for-the-badge&color=e0314f&label=vers%C3%A3o"></a>
<img alt="Windows" src="https://img.shields.io/badge/Windows-x64%20%C2%B7%20DirectX%2012-0078D4?style=for-the-badge&logo=windows">
<img alt="Android" src="https://img.shields.io/badge/Android-ARM64%20%C2%B7%20Vulkan-3DDC84?style=for-the-badge&logo=android&logoColor=white">
<br>
<img alt="EUA" src="https://img.shields.io/badge/EUA-BLUS31313%201.40-2554C7?style=flat-square">
<img alt="Europa" src="https://img.shields.io/badge/Europa-BLES01939%201.00-2554C7?style=flat-square">
<img alt="Japão" src="https://img.shields.io/badge/Jap%C3%A3o-BLJS10206%201.30%20(experimental)-b45f00?style=flat-square">
<img alt="Idiomas" src="https://img.shields.io/badge/launcher-PT%20%C2%B7%20EN-6a3fd6?style=flat-square">
<img alt="App Android" src="https://img.shields.io/badge/app%20Android-EN%20%C2%B7%20PT%20%C2%B7%20JA%20%C2%B7%20ES-6a3fd6?style=flat-square">
<a href="https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues"><img alt="Ajuda bem-vinda" src="https://img.shields.io/badge/ajuda-bem--vinda-ff5e73?style=flat-square"></a>
</p>

<p align="center">
<a href="https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/latest"><b>⬇️ Baixar / Download</b></a> ·
<a href="#-galeria--gallery">🖼️ Galeria</a> ·
<a href="#-compatibilidade--compatibility">🎮 Compatibilidade</a> ·
<a href="https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues">🐞 Reportar erro</a>
</p>

---

## 🇧🇷 Português

> **Assinatura de código:** veja a [política de assinatura](CODE_SIGNING_POLICY.md). Alguns antivírus marcam o launcher por engano (detecção genérica de programas Python empacotados); os hashes SHA-256 de cada versão estão na release.

> **Projeto comunitário em melhoria contínua. Qualquer ajuda é bem-vinda**: testes em outros aparelhos e versões do jogo, relatos de erro com log, capturas de tela, tradução e código.
> **Nenhum arquivo do jogo, atualização, firmware, chave, save ou código compilado do jogo é distribuído.** Você precisa da sua própria cópia. Projeto independente, sem vínculo com os detentores dos direitos.

### ✨ Destaques

- 🖥️ **Windows** (DirectX 12): launcher em vidro, perfis EUA/Europa/Japão, FSR 1, janela/tela cheia sem bordas, VSync, saves em slots, cheats e controles configuráveis.
- 📱 **Android** (Vulkan): testado no AYN Odin 2 Portal, 30–60 fps, controles na tela, menu **L3 + R3**, saves em slots, cheats e editor de personagens.
- 🏆 **Troféus no estilo PlayStation** com nome e ícone originais (Android; Windows na 1.0.53), com trava opcional para quem usa cheats.
- 🌍 Launcher em **Português/English**; app Android em **English, Português, 日本語 e Español**.

### 🎮 Compatibilidade / Compatibility

| Versão do jogo | Windows | Android |
|---|:---:|:---:|
| **EUA** · BLUS31313 1.40 | ✅ Funciona | ⚪ Não testada |
| **Europa** · BLES01939 1.00 | ✅ Funciona | ✅ Funciona |
| **Japão** · BLJS10206 1.30 | 🧪 Até o 1º mapa | ⚪ Não testada |

✅ testado · 🧪 experimental · ⚪ sem teste no aparelho (ajuda bem-vinda)

### 📥 Instalação rápida

<table>
<tr><td width="50%" valign="top">

**🖥️ Windows**
1. Baixe o instalador (`DisgaeaD2Launcher-Setup-Windows-x64.exe`) ou o ZIP em [Releases](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/latest).
2. Abra o launcher e escolha **a sua** ISO ou pasta do jogo (EUA: também o PKG da atualização 1.40).
3. A primeira preparação compila o jogo no seu PC (internet e tempo necessários).
4. Clique em **JOGAR**.

</td><td width="50%" valign="top">

**📱 Android**
1. No launcher do Windows: **Arquivos e suporte → Preparar núcleo Android** (gera o `libd2core.so` do **seu** jogo).
2. Instale o APK de [Releases](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/latest).
3. Copie a pasta do jogo (`PS3_GAME`) para `Android/data/org.disgaead2.port/files/game`.
4. No app: **Escolher núcleo** → `libd2core.so` → **JOGAR**.

</td></tr>
</table>

Saves e configurações do Windows ficam em `%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview` (não apague para atualizar). Detalhes: [guia do launcher](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/blob/eu-preview/docs/LAUNCHER-WINDOWS.md) · [controles](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/blob/eu-preview/docs/CONTROLS.md) · [gráficos](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/blob/eu-preview/docs/GRAPHICS.md) · [versão japonesa](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/blob/eu-preview/docs/JAPANESE.md).

### 🖼️ Galeria / Gallery

**🖥️ Windows**

<table>
<tr><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/jogo-titulo.jpg" width="300"><br><sub>Título / Title</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/jogo-cidade.jpg" width="300"><br><sub>Cidade / Town</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/jogo-quarto.jpg" width="300"><br><sub>Quarto / Room</sub></td></tr>
<tr><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/jogo-batalha-ataque.jpg" width="300"><br><sub>Batalha / Battle</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/jogo-batalha-alvo.jpg" width="300"><br><sub>Alvo / Targeting</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/launcher-inicio.png" width="300"><br><sub>Launcher</sub></td></tr>
</table>

<table>
<tr><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/launcher-video.png" width="300"><br><sub>Vídeo, FSR, resolução</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/launcher-controles.png" width="300"><br><sub>Controles / Controls</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/launcher-iso-vidro.png" width="300"><br><sub>Escolher ISO / Pick ISO</sub></td></tr>
</table>

**📱 Android** (AYN Odin 2 Portal)

<table>
<tr><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/00-tela-inicial.jpg" width="300"><br><sub>Tela inicial / Home</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/01-titulo.jpg" width="300"><br><sub>Título / Title</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/03-corredor.jpg" width="300"><br><sub>Corredor / Hallway</sub></td></tr>
<tr><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/05-selecao-de-area-60fps.jpg" width="300"><br><sub>Seleção de área · 60 fps</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/08-batalha-60fps.jpg" width="300"><br><sub>Batalha · 60 fps</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/controles-na-tela.jpg" width="300"><br><sub>Controles na tela / On-screen controls</sub></td></tr>
</table>

<table>
<tr><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/menu-video.jpg" width="300"><br><sub>Vídeo</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/menu-filtros.jpg" width="300"><br><sub>Filtros / Filters</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/menu-cheats.jpg" width="300"><br><sub>Cheats</sub></td></tr>
<tr><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/menu-saves.jpg" width="300"><br><sub>Saves</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/editor-personagens-valores.jpg" width="300"><br><sub>Editor de personagens</sub></td><td align="center"><img src="https://raw.githubusercontent.com/LUUKASR1/disgaea-d2-recomp-windows/03146694be21e4c2a3b1980aa553e72b33c69688/docs/images/android/menu-sistema.jpg" width="300"><br><sub>Sistema / System</sub></td></tr>
</table>

### 🧩 Recursos por plataforma

| | 🖥️ Windows | 📱 Android |
|---|---|---|
| Gráficos | DirectX 12, FSR 1, resolução interna/saída | Vulkan, resolução interna, filtros (CRT, FXAA…) |
| Controles | Teclado, controle, guia de comandos | Controles na tela + controle físico |
| Saves | Slots, backup automático | Slots, importar saves do PC, backup automático |
| Cheats | Janela **F1** com personagens e troféus (1.0.53) | Menu **L3 + R3**, editor de personagens |
| Troféus | Aviso PlayStation e trava opcional (1.0.53) | Aviso PlayStation, resetar e trava |
| Idiomas | Português · English | English · Português · 日本語 · Español |

### 🤝 Como ajudar

Abra uma [issue](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues) com o **log** (Windows: `preparacao.log`; Android: **Compartilhar log**) e, se possível, uma captura de tela. Testes da versão americana e japonesa no Android, de outros aparelhos e de outras partes da campanha são muito úteis. Pull requests são bem-vindos.

### ℹ️ Sobre

Nasceu da paixão por Disgaea, em especial **Disgaea D2**, e é uma aventura de recompilação feita com ajuda de IA (direção e testes humanos). Parte do projeto original de [Sean13128](https://github.com/Sean13128/disgaea-d2-recomp) e do [ps3recomp](https://github.com/sp00nznet/ps3recomp); o suporte japonês foi contribuído por DesertDoggy. Créditos completos em [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md) · [LICENSE](LICENSE) · [histórico de testes](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/blob/eu-preview/docs/STATUS-WINDOWS.md).

---

## 🇺🇸 English

> **Code signing:** see the [code signing policy](CODE_SIGNING_POLICY.md). Some antivirus engines flag the launcher by mistake (a generic detection of packaged Python programs); the SHA-256 hashes of every version are in the release.

> **Community project, improved continuously. Any help is welcome**: testing on other devices and game versions, bug reports with logs, screenshots, translations and code.
> **No game files, updates, firmware, keys, saves or compiled game code are distributed.** You need your own copy. Independent project, not affiliated with the rights holders.

**Highlights.** 🖥️ **Windows** (DirectX 12): glass launcher, US/EU/JP profiles, FSR 1, borderless/fullscreen, save slots, cheats. 📱 **Android** (Vulkan): tested on the AYN Odin 2 Portal at 30–60 fps, on-screen controls, **L3 + R3** menu, save slots, cheats and character editor. 🏆 PlayStation-style trophy pop-up with the original names and icons. 🌍 Launcher in Portuguese/English; Android app in English, Portuguese, Japanese and Spanish.

**Compatibility.** US 1.40: Windows ✅, Android not tested. Europe 1.00: Windows ✅, Android ✅. Japan 1.30: Windows 🧪 (tested up to the first map), Android not tested.

**Quick start — Windows.** Download the installer or portable ZIP from [Releases](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/latest), open the launcher, pick **your** ISO or game folder (US also needs the 1.40 update PKG), let the first preparation build the game on your PC, then press **PLAY**.

**Quick start — Android.** Build `libd2core.so` from your own game with the Windows launcher (**Files and support → Prepare Android core**), install the APK from [Releases](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/latest), copy your game folder (with `PS3_GAME`) to `Android/data/org.disgaead2.port/files/game`, pick the core in the app and press **PLAY**.

**Help.** Open an [issue](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues) with your log and a screenshot if possible. Pull requests are welcome.

**Credits.** Based on [Sean13128/disgaea-d2-recomp](https://github.com/Sean13128/disgaea-d2-recomp) and [ps3recomp](https://github.com/sp00nznet/ps3recomp); Japanese support contributed by DesertDoggy. See [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md) and [LICENSE](LICENSE).
