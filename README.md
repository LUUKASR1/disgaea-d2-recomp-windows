<img width="1920" height="1080" alt="Sem-Título-1" src="https://github.com/user-attachments/assets/97307552-12cf-4b83-ad21-375dac0d4432" />

# Disgaea D2 static recompilation — Windows candidate

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
