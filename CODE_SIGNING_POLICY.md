# Code signing policy / Política de assinatura de código

Free code signing provided by [SignPath.io](https://signpath.io), certificate by [SignPath Foundation](https://signpath.org).

> Status: the project is applying to the SignPath Foundation open-source program. Until the application is approved,
> release files are **not** signed. This file describes how signing will work once it is active.

## Scope

Only these Windows release files, built from this repository, are signed:

- `DisgaeaD2Launcher.exe`
- `DisgaeaD2Launcher-Setup-Windows-x64.exe`

The Android APK, the game executable that the launcher compiles on the user's own computer, and third-party files are not
signed with this certificate.

## How release files are built

- Signed files are built only by GitHub Actions (`.github/workflows/official-release-eu-preview.yml`) on GitHub-hosted
  runners, from a commit of the public repository `LUUKASR1/disgaea-d2-recomp-windows`.
- The build is fully automated. Each release publishes `SHA256SUMS.txt` and `release-build.json` with the source commit.
- No game content is included. The launcher only prepares and starts a build made from the player's own game files.

## Roles

| Role | Member |
|---|---|
| Committers and reviewers | [LUUKASR1](https://github.com/LUUKASR1) |
| Approvers | [LUUKASR1](https://github.com/LUUKASR1) |

Every signing request is approved manually by an approver. All members use multi-factor authentication on GitHub and SignPath.

## Privacy

The launcher does not send any data to the project or to third parties. Its only network requests download tools and
files it needs to prepare the game, each from its official source: the PS3 system software (firmware) from Sony's update
server, RPCS3 and 7-Zip from their GitHub releases, and the Android NDK when the user prepares the Android core.
