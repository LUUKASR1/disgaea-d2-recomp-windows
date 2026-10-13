<img width="1920" height="1080" alt="Sem-Título-1" src="https://github.com/user-attachments/assets/60258dd9-584b-4cd2-aa43-afb01ee681fc" />

# Disgaea D2 WINPORT (PROJETO EM DESENVOLVIMENTO)

## Começar no Windows

Baixe o instalador ou a versão portátil em [Releases](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases). O launcher atual é o **0.1.10-preview**. A primeira preparação gera e compila o jogo no seu computador; as sessões seguintes reutilizam essa build.

Você precisa da sua própria cópia **extraída** do jogo americano **BLUS31313** e do PKG de atualização **US 1.40**. DLCs são opcionais e precisam dos seus próprios arquivos e licenças. A entrada direta de ISO ainda não está implementada.

1. Instale `DisgaeaD2Launcher-Setup-Windows-x64.exe` e abra o launcher. Se usar o ZIP portátil, extraia tudo e mantenha a pasta `_internal` junto do executável.
2. Clique em **Jogar**, selecione a pasta do jogo (ou `PS3_GAME`) e a atualização 1.40; inclua seus PKGs de DLC, se desejar. Também pode selecionar esses arquivos na aba **Preparação**.
3. Aguarde a primeira preparação. O launcher baixa ferramentas portáteis, o auxiliar oficial e o firmware PS3 oficial verificado; gera a build local e inicia o jogo. Não é necessário instalar Git, Visual Studio ou LLVM manualmente. A primeira vez requer internet e pode demorar; nas próximas, **Jogar** reutiliza a build. Firmware próprio ou do RPCS3 continua disponível como opção avançada. Se faltarem runtimes de execução C++/Vulkan no Windows, os instaladores oficiais são obtidos automaticamente; o Windows pode pedir autorização de administrador. Um driver de vídeo adequado continua necessário.

**Já tem uma build funcionando?** Feche o jogo e o launcher, instale a atualização do launcher e clique em **Jogar**. Na atualização para 0.1.10, a build compatível e os saves são preservados; não é necessário recompilar ou importar novamente os pacotes. Não apague `%LOCALAPPDATA%\DisgaeaD2Recomp`.

Se o launcher apontar um arquivo ausente ou vazio, restaure-o da sua própria cópia extraída. A verificação cobre 18 arquivos de início e não substitui uma conferência completa dos dados. Consulte o [guia do launcher](docs/LAUNCHER-WINDOWS.md) para os detalhes.

A aba **Vídeo** oferece resolução de exibição em janela, tela cheia sem bordas e VSync. A resolução de exibição amplia a imagem; a resolução interna permanece original. As opções são aplicadas na próxima abertura do jogo.

## Getting started on Windows

Download the installer or portable package from [Releases](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases). The current launcher is **0.1.10-preview**. The first preparation generates and compiles the game on your computer; later sessions reuse that build.

You need your own **extracted** US game copy (**BLUS31313**) and the **US 1.40** update PKG. DLCs are optional and require your own files and licenses. Direct ISO input is not implemented yet.

1. Install `DisgaeaD2Launcher-Setup-Windows-x64.exe` and open the launcher. For the portable ZIP, extract everything and keep `_internal` next to the executable.
2. Click **Jogar**, select the game folder (or `PS3_GAME`) and the 1.40 update; add your DLC PKGs if desired. These files can also be selected in the **Preparação** tab.
3. Wait for the first preparation. The launcher downloads private portable tools, the official helper and verified official PS3 firmware, generates the local build, and starts the game. No manual Git, Visual Studio or LLVM installation is needed. The first run requires internet and can take time; later sessions reuse the build. Importing your existing firmware remains an advanced option. Missing Windows C++/Vulkan runtimes are obtained through official installers; Windows may request administrator permission. A suitable graphics driver is still required.

**Already have a working build?** Close the game and launcher, install the launcher update, and click **Jogar**. Updating to 0.1.10 preserves your compatible build and saves; no recompilation or package reimport is needed. Do not delete `%LOCALAPPDATA%\DisgaeaD2Recomp`.

If the launcher reports a missing or empty file, restore it from your own extracted copy. This check covers 18 startup files and does not validate all game data. See the [launcher guide](docs/LAUNCHER-WINDOWS.md) for details.

The **Vídeo** tab offers window display resolution, borderless fullscreen, and VSync. Display resolution scales the image; internal rendering resolution remains unchanged. Settings apply on the next game launch.

## Sobre o projeto

Este projeto nasceu da minha paixão por Disgaea — especialmente por **Disgaea D2: A Brighter Darkness**. É a minha primeira aventura no mundo da recompilação de jogos com ajuda de IA. Recomp-IA? Vibe coding? Ainda estou descobrindo o nome, mas estou aprendendo bastante no caminho.

Passei praticamente o domingo inteiro mexendo nisso. A intenção não era ficar o dia todo, mas o progresso estava tão satisfatório que eu não consegui parar. Pelo menos saiu algo! 😅

Durante esse processo, apareceram vários desafios: tela branca, quedas de FPS no Castle Hallway e no ataque básico do Mothman, problemas nas texturas e outros glitches gráficos. Deu trabalho para mim e para a nossa querida IA encontrar as causas e testar as correções. Conseguimos resolver muita coisa, e continuo trabalhando para melhorar a estabilidade e o desempenho da versão Windows.

Como trabalho e tenho outros afazeres, vou atualizar o projeto regularmente, conforme meu tempo permitir. Quem quiser experimentar, relatar problemas ou contribuir com melhorias está à vontade. Toda ajuda para fazer o projeto avançar é bem-vinda!

Também quero explorar uma versão para portáteis Android, como o **AYN Odin 2 Portal** e o **AYN Thor**. Esse é um objetivo futuro: primeiro, quero deixar a versão Windows em um estado satisfatório. Depois, pretendo me dedicar à portabilidade assim que possível.

Este trabalho parte do projeto original de **Sean13128** e do **ps3recomp**, com os devidos créditos aos seus autores e colaboradores. Minha contribuição aqui está na adaptação para Windows, nos ajustes, no launcher e nos testes, com auxílio de IA.

## About the project

This project grew out of my love for Disgaea — especially **Disgaea D2: A Brighter Darkness**. This is my first adventure into game recompilation with the help of AI. Recomp-AI? Vibe coding? I'm still figuring out what to call it, but I'm learning a lot along the way.

I spent pretty much the whole of Sunday working on this. I hadn't planned to spend the entire day on it, but the progress was so satisfying that I couldn't stop. At least I have something to show for it! 😅

Along the way, I ran into several challenges: a white screen, FPS drops in Castle Hallway and during Mothman's basic attack, texture issues, and other graphical glitches. It took a lot of work from me and our dear AI to find the causes and test the fixes. We managed to resolve many of these issues, and I'm continuing to improve the stability and performance of the Windows version.

Since I have a job and other responsibilities, I'll update the project regularly as my time allows. If you'd like to try it out, report issues, or contribute improvements, you're more than welcome. Any help moving the project forward is appreciated!

I'd also like to explore a version for Android handhelds such as the **AYN Odin 2 Portal** and the **AYN Thor**. That's a future goal: first, I want to get the Windows version into a satisfactory state. After that, I plan to work on bringing it to handhelds as soon as I can.

This work builds on the original project by **Sean13128** and on **ps3recomp**, with full credit to their authors and contributors. My contribution here focuses on the Windows adaptation, fixes, launcher, and testing, with assistance from AI.

## Technical overview

Experimental Windows x64 port of [Sean13128/disgaea-d2-recomp](https://github.com/Sean13128/disgaea-d2-recomp), based on [ps3recomp](https://github.com/sp00nznet/ps3recomp). Current target: US game update 1.40, Direct3D 12, clang-cl. Android is not supported by this candidate yet.

**The repository contains source code, and releases provide the launcher and its dependencies. No game executable, generated game code, game data, updates/DLC, keys or saves are distributed.** Generate and compile the game locally from your own copy. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Automatic preparation in an empty application folder was confirmed on the main PC using already installed compilers: firmware, US 1.40 update, code generation, and all 199 build steps completed. DLC import and save/load were also confirmed in that build. The second PC completed compilation with Clang 19.1.5; after restoring missing audio files, the user confirmed gameplay, save/load, DLCs, and closing. The user also confirmed that the 0.1.7 launcher update works. Windows CI passed 21 tests and installer/reinstall/uninstall checks with preservation of test saves and configuration.

On the main PC (Ryzen 7 5700X3D / RTX 5070 / 32 GB RAM), the user reported 55–60 FPS in a later test, without a controlled frametime capture. No FPS measurement was reported for the second PC. This remains experimental: locked 60 FPS, the entire campaign, every DLC, and the full tool installation process on a completely clean Windows system have not been validated. Android is a future goal. See the [dated test history](docs/STATUS-WINDOWS.md).

- [Windows GUI launcher: select your extracted game and update PKGs](docs/LAUNCHER-WINDOWS.md) (experimental; first-run local compilation)
- [Windows build and run](docs/WINDOWS.md)
- [Observed status and performance](docs/STATUS-WINDOWS.md)
- [Windows changes](docs/CHANGELOG-WINDOWS.md)
- [Original project documentation](docs/UPSTREAM-README.md)

## Visão geral técnica

Adaptação experimental para Windows x64 do projeto [Sean13128/disgaea-d2-recomp](https://github.com/Sean13128/disgaea-d2-recomp), baseada no [ps3recomp](https://github.com/sp00nznet/ps3recomp). O alvo atual é a versão americana do jogo com a atualização 1.40, usando Direct3D 12 e clang-cl. Esta versão candidata ainda não oferece suporte a Android.

**O repositório contém o código-fonte, e as releases oferecem o launcher e suas dependências. Não são distribuídos o executável do jogo, código gerado a partir do jogo, dados do jogo, atualizações/DLCs, chaves ou saves.** Gere e compile o jogo localmente a partir da sua própria cópia. Consulte [LICENSE](LICENSE) e [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

A preparação automática numa pasta vazia do aplicativo foi confirmada no PC principal, usando compiladores já instalados: firmware, atualização US 1.40, geração de código e as 199 etapas de compilação foram concluídos. A importação de DLCs e o save/load também foram confirmados nessa build. O segundo PC concluiu a compilação com Clang 19.1.5; após restaurar arquivos de áudio ausentes, o usuário confirmou o jogo, save/load, DLCs e fechamento. O usuário também confirmou o funcionamento da atualização do launcher para 0.1.7. A validação automática no Windows passou nos 21 testes e nas verificações de instalação, reinstalação e desinstalação, preservando os saves e a configuração de teste.

No PC principal (Ryzen 7 5700X3D / RTX 5070 / 32 GB de RAM), o usuário relatou 55–60 FPS num reteste posterior, sem captura controlada de frametimes. Não foi informado FPS no segundo PC. O projeto continua experimental: ainda não foram validados 60 FPS travados, a campanha inteira, cada DLC e todo o processo de instalação de ferramentas num Windows completamente limpo. Android é um objetivo futuro. Consulte o [histórico datado dos testes](docs/STATUS-WINDOWS.md).

- [Launcher com interface gráfica para Windows: selecione seu jogo extraído e os PKGs de atualização](docs/LAUNCHER-WINDOWS.md) (experimental; compilação local na primeira preparação)
- [Compilar e executar no Windows](docs/WINDOWS.md)
- [Estado observado e desempenho](docs/STATUS-WINDOWS.md)
- [Alterações para Windows](docs/CHANGELOG-WINDOWS.md)
- [Documentação do projeto original](docs/UPSTREAM-README.md)

Créditos: Sean13128 e colaboradores pelo projeto original; Ned Heller / sp00nznet e colaboradores pelo ps3recomp; respectivos autores pelas bibliotecas de terceiros. Desenvolvimento assistido por IA, com direção e testes manuais do usuário. Mantenedor e testes Windows: [LUUKASR1](https://github.com/LUUKASR1). Projeto independente e não oficial, sem afiliação com os titulares do jogo.
