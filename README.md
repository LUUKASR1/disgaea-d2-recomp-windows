<img width="1920" height="1080" alt="Sem-Título-1" src="https://github.com/user-attachments/assets/60258dd9-584b-4cd2-aa43-afb01ee681fc" />

# Disgaea D2 — Windows / DirectX 12

## Versão pública oficial do projeto — 0.2.1 Stable

Baixe o **instalador** ou o **ZIP portátil** em [Releases](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/latest).
Idioma do launcher: **Português (Brasil) / English**, com seleção abaixo de WIN PORT.
Selecione sua própria **ISO USA compatível** e o PKG da atualização **BLUS31313 US 1.40**; DLCs são opcionais. A primeira preparação baixa ferramentas portáteis e compila o jogo automaticamente no seu computador. Precisa de internet e pode demorar. Não requer instalar ferramentas manualmente nem importar firmware.

**DirectX 12**, configurações de resolução interna/saída, janela/tela cheia sem bordas, VSync, Hz e FSR 1 espacial. Vulkan, DLSS, Frame Generation e Android ainda não estão disponíveis. HUD após upscaling permanece experimental e desligado por padrão.

[Instalação, saves, requisitos e limitações / Installation, saves, requirements and limitations](docs/RELEASE-0.2.1.md) · [Reportar erro / Report a bug](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml)

A versão oficial reutiliza `%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview` por compatibilidade com a linha testada. Não apague essa pasta. Outros perfis são preservados, sem migração universal automática.

## Official project release — 0.2.1 Stable

Launcher languages: **Português (Brasil) / English**, selected below WIN PORT.

Download the **installer** or **portable ZIP** from [Releases](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/releases/latest). Provide your own supported US ISO, **BLUS31313 US 1.40** update PKG and optional DLCs. First-run downloads and local compilation are automatic and require internet. DirectX 12 only; Vulkan, DLSS, Frame Generation and Android are unavailable. See the [release guide](docs/RELEASE-0.2.1.md) for requirements, save compatibility and known limitations.

**No game data, updates/DLC, firmware, keys, saves or game-derived compiled code is distributed.** Independent community project, not an official release from the game's rights holders.

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

Windows x64 adaptation of [Sean13128/disgaea-d2-recomp](https://github.com/Sean13128/disgaea-d2-recomp), based on [ps3recomp](https://github.com/sp00nznet/ps3recomp). The official 0.2.1 launcher uses DirectX 12 and generates the recognized US 1.40 runtime locally from user-provided files. The GPU command translation layer remains part of the runtime. A launcher release is not evidence that the entire campaign, every DLC or every PC configuration has been validated.

Vulkan and temporal upscaling development remain experimental and separate from the public playable renderer. See [LICENSE](LICENSE), [third-party credits](THIRD_PARTY_NOTICES.md), [test history](docs/STATUS-WINDOWS.md) and the [0.2.1 guide](docs/RELEASE-0.2.1.md).

## Visão geral técnica

Adaptação Windows x64 do projeto de Sean13128, baseada no ps3recomp. O launcher oficial 0.2.1 usa DirectX 12 e gera o runtime US 1.40 reconhecido no computador do usuário. A camada de tradução dos comandos gráficos continua fazendo parte do runtime. A campanha inteira, todas as DLCs e todas as configurações de PC não foram validadas. O desenvolvimento Vulkan e temporal continua separado do renderizador público jogável.

Créditos: Sean13128 e colaboradores pelo projeto original; Ned Heller / sp00nznet e colaboradores pelo ps3recomp; respectivos autores pelas bibliotecas de terceiros. Desenvolvimento assistido por IA, com direção e testes manuais do usuário. Mantenedor e testes Windows: [LUUKASR1](https://github.com/LUUKASR1). Projeto independente, sem afiliação com os titulares do jogo.
