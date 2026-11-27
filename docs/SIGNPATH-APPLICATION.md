# SignPath Foundation application (draft)

Answers to paste into the form at https://signpath.org (button **Apply**). Review each one before sending.

**Project name:** Disgaea D2 PC Port (Disgaea D2: A Brighter Darkness — Recomp)

**Repository:** https://github.com/LUUKASR1/disgaea-d2-recomp-windows

**License:** MIT (OSI-approved). Bundled third-party components keep their own open-source licenses, listed in
`THIRD_PARTY_NOTICES.md` (ps3recomp SDK: MIT; nlohmann/json: MIT; CMake: BSD-3; Ninja: Apache-2.0; libadrenotools: BSD-2).

**What the software does:** a launcher for Windows that turns the player's own copy of the PS3 game *Disgaea D2* into a
native Windows build. The player supplies their own game files. The launcher recompiles the game on the player's computer
and starts it. No game content, game code or copyrighted assets of the game are distributed in the repository or in the
release files.

**Files to sign:** `DisgaeaD2Launcher.exe` and `DisgaeaD2Launcher-Setup-Windows-x64.exe` (Inno Setup installer).

**Build:** fully automated on GitHub-hosted runners with GitHub Actions
(`.github/workflows/official-release-eu-preview.yml`). The launcher is packaged with PyInstaller. Each release publishes
SHA-256 checksums and the source commit in `release-build.json`.

**Code signing policy:** `CODE_SIGNING_POLICY.md` in the repository root, linked from the README.

**Team:** LUUKASR1 (owner, committer, reviewer and approver). Multi-factor authentication is enabled on GitHub and will
be enabled on SignPath.

**Prior releases:** several stable releases on GitHub (latest: v1.0.53).

**Why we need signing:** some antivirus engines flag the unsigned launcher with generic detections (packaged Python
programs). A signature that ties the binary to this public repository lets users verify where the file comes from.

## Before sending

1. Turn on two-factor authentication on GitHub (Settings → Password and authentication) if it is not on yet.
2. Make sure `CODE_SIGNING_POLICY.md` and the README link are on the default branch (`main`); SignPath checks the public
   repository.
3. Expect a question about the game: answer that the repository holds only the launcher, the recompiler tooling and
   patches, and that the game itself is never distributed.
4. **Resolve this first:** the repository and the packaged launcher contain artwork from the game (for example
   `android/app/src/main/res/drawable-nodpi/home_art.jpg`, `home_title.png`, launcher branding images), screenshots in
   `docs/` and trophy names in `windows/trophy_names.py` / `TrophyNames.java`. SignPath requires that signed packages hold
   no proprietary components. Replace the game artwork in the launcher and app with original art (or load it from the
   player's own game files at run time, as the trophy icons already are) before applying. Screenshots in `docs/` are not
   part of the signed files, but reviewers may still ask about them.
