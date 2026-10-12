<img width="1920" height="1080" alt="Sem-Título-1" src="https://github.com/user-attachments/assets/60258dd9-584b-4cd2-aa43-afb01ee681fc" />

# Disgaea D2 static recompilation — Windows candidate

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

**Source-only candidate; no game executable, generated game code, game data, updates/DLC, keys or saves are distributed.** Generate and compile locally from your own copy. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Tested by the user on Ryzen 7 5700X3D / RTX 5070 / 32 GB RAM. Audio and effects were normal; remaining Mothman attack drops prevent a stable 60 FPS claim. The Windows close correction was verified on the user's PC: process exit code 0, observed within approximately 0.1 seconds after window closure. No active savedata operation was observed during that close. Save, close, reopen and load were manually confirmed by LUUKASR1. A clean Windows build is still pending; this is a development candidate.

- [Windows GUI launcher: select your extracted game and update PKGs](docs/LAUNCHER-WINDOWS.md) (experimental; first-run local compilation)
- [Windows build and run](docs/WINDOWS.md)
- [Observed status and performance](docs/STATUS-WINDOWS.md)
- [Windows changes](docs/CHANGELOG-WINDOWS.md)
- [Original project documentation](docs/UPSTREAM-README.md)

## Português

Esta candidata organiza as fontes da build Windows 1.40 e consolida os ajustes do SDK num patch com revisão e checksum fixos. Os scripts de build geram o código localmente, usando o ELF do usuário. O fechamento por Alt+F4 foi confirmado no Windows. Salvar, reabrir e carregar foram confirmados pelo usuário. Ainda é necessário validar uma compilação limpa antes de anunciar uma release reproduzível.

Créditos: Sean13128 e colaboradores pelo projeto original; Ned Heller / sp00nznet e colaboradores pelo ps3recomp; respectivos autores pelas bibliotecas de terceiros. Desenvolvimento assistido por IA, com direção e testes manuais do usuário. Mantenedor e testes Windows: [LUUKASR1](https://github.com/LUUKASR1). Projeto independente e não oficial, sem afiliação com os titulares do jogo.
