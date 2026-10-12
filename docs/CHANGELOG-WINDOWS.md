# Launcher 0.1.7-preview

- Check presence and nonzero size of 18 startup files in the extracted US game
  before preparation, cached build reuse and launching. Report missing/empty paths.
- Include the missing SndPakBgm_0.pak that blocked loading on the second PC.
- Log file validation failures without changing game files, cached build configuration or saves.
- Twenty-one tests pass locally, including blocked preparation/launch and save preservation.
- Record second-PC compilation with Clang 19.1.5 and the user's successful
  run/save/load/DLC/close confirmation after restoring the audio folder.
- This is a startup presence/size check, not whole-game integrity validation.
  No runtime or graphical profile changes.

# Launcher 0.1.6-preview

- Find all complete C++ Visual Studio installations, ordered newest first,
  and select one with an available clang-cl instead of always using the latest.
- Fix the observed 2026-without-LLVM / 2022-with-LLVM detection failure on the
  second user's PC. Support standalone LLVM in its standard Program Files folder.
- Fall back when an instance lacks vcvarsall/clang-cl or cannot initialize its environment.
- Record the selected Visual Studio and resolved tool paths, or the exact detection
  failure, in preparacao.log without logging the full environment.
- Seventeen preparation/detection tests pass locally; real second-PC compilation
  with this selection fix remains pending. No runtime or profile changes.

# Launcher 0.1.5-preview

- Recover helper exit 3221226505 after a fresh success log for the same
  NPUB31321/01.00 flag package and a complete installed file inventory.
- Require PARAM.SFO and at least one known flag EDAT; reject missing/empty files,
  unexpected payloads, paths outside the content folder, stale logs and other failures.
- Keep the ELF SHA-256 gate, original return code and warning; reuse compatible builds.
- Record save/load and DLC recognition confirmed in the isolated 0.1.4 build.
- Twelve preparation tests passed on Windows CI; a real 0.1.5 retest confirmed
  automatic DLC reimport, ELF validation, existing build reuse and game exit code 0.
- No runtime, graphical profile, game asset or license changes.

# Launcher 0.1.4-preview

- Recover the observed helper exit 3221226505 only after a fresh firmware
  success log and installed modules, or an exact US 1.40 package success log
  and installed update files. Preserve the original return code and a warning.
- Keep the supported ELF SHA-256 check mandatory after package recovery.
- Reject stale success logs, other packages/versions, missing outputs, other
  failures and timeouts. DLC installation failures are not covered by this workaround.
- Record the user's successful isolated 0.1.3 compilation and two game exits
  with code 0. A subsequent fresh 0.1.4 test confirmed automatic firmware/update
  recovery, 199/199 build completion and a brief game run/exit with code 0.
  It reused installed compilers; individual DLCs and a toolchain-free PC remain untested.
- No native runtime, graphical profile or game code changes.

# Windows candidate 0.1

Unpublished development candidate based on original project commit
`340cbed43cf8427005bcfcbf537e65190161ead8` and SDK base
`a679051ef304555291de2eb3ec8a3dbf64a761a8`.

- Windows build integration, Direct3D 12 renderer and WASAPI audio.
- Texture/font upload, pitch and palette corrections.
- Depth/stencil state correction for attack selection and effects.
- Save transaction replacement/commit correction on Windows.
- Graphics SPU idle handling, constant caches and adjacent particle grouping.
- Optional SSE4.1 for existing lifted SPU helper implementations.
- Host diagnostics and lightweight frame sampling for local investigation.
- Windows close handling through savedata admission/quiescence, verified on the user’s Windows PC (exit code 0). Ends the host process after savedata completes;
  does not yet implement cooperative guest worker stop/join.
- Consolidated Windows SDK patch, separate pinned lock, Windows bootstrap,
  fresh-build command and normal launcher.

No new performance result is claimed for the close correction. Remaining
Mothman attack drops are documented in STATUS-WINDOWS.md.
Android/Odin 2 Portal support is future work.

Save/reopen/load confirmed manually by LUUKASR1 after the close correction.
A clean build from the consolidated source-only candidate is still pending.

## Launcher 0.1.8-preview

- Interface personalizada em Tk: abas Início, Vídeo, Preparação e Arquivos e suporte.
- Resolução de exibição em janela, tela cheia sem bordas e VSync no launcher.
- Preferências validadas e persistidas separadamente; opção de restaurar padrão.
- Ajuste de janela limitado ao PID iniciado e à classe ps3recomp_d3d12.
- A imagem é ampliada; resolução interna e runtime permanecem iguais. Sem nova compilação para builds compatíveis.
- Testes de persistência, geometria, ambiente, processo, GUI e Win32 sem dados do jogo. Validação real dos novos modos permanece pendente.

## Launcher 0.1.9-preview

- Logo WIN PORT fornecida pelo usuário na tela inicial, com transparência e proporção preservadas.
- Imagem quadrada fornecida pelo usuário na barra lateral e como ícone da janela, executável, instalador e atalhos.
- Paleta vermelha, violeta e laranja alinhada à arte; barra de rolagem escura.
- Configurações de vídeo, runtime, builds e saves preservados. Sem necessidade de recompilar uma build compatível.
