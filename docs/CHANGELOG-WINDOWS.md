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
