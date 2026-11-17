# Disgaea D2 Windows 1.0.15 — Prévia da versão europeia (BLES01939)

> **Pré-lançamento experimental.** A versão estável continua sendo a [1.0.2](RELEASE-1.0.2.md). Esta prévia existe para
> testar o jogo europeu; pode falhar na preparação ou no jogo.

## Português

### O que há de novo

- **Perfil europeu (BLES01939)** na página **Arquivos e suporte → Versão europeia**: escolha a **pasta do jogo (PS3_GAME) ou a ISO europeia** e o
  **EBOOT.elf descriptografado** do seu disco; o launcher compila o jogo no seu PC (nada do jogo é distribuído).
- Perfis **americano (1.40)** e **europeu** ficam salvos separadamente; troque com **Usar perfil europeu / americano**.
- O executável europeu aceito tem SHA256 `0257bc3ab36a63f55b0a8722fa54f7d5278a568941bdff7817934a891fcb0cbb`
  (disco e atualização europeia têm o mesmo executável).

### Novidades da 1.0.15

- A preparação europeia já gera o código do jogo (10.374 funções) e passa pelas verificações; esta versão corrige o
  passo seguinte da compilação, que falhava em `spu-eu`.

### Limites conhecidos

- Usa o layout do executável 1.00. Se algum ajuste do projeto (cheats, etc.) não casar com o código europeu, a
  compilação avisa e para; mande o log (`Abrir pasta local do aplicativo` → `preparacao.log`).
- Aceita a pasta do jogo ou a ISO do disco; dados da atualização europeia (PKG) ainda não são importados.
- A primeira preparação demora bastante (compila o jogo).

## English

**Experimental pre-release.** Stable remains [1.0.2](RELEASE-1.0.2.md).

- **European profile (BLES01939)** under **Files and support → European version**: pick the European game folder (PS3_GAME) or ISO and the
  decrypted EBOOT.elf from your disc; the launcher compiles the game on your PC (no game content is distributed).
- US 1.40 and European profiles are saved separately; switch with **Use European / US profile**.
- Accepted European executable SHA256: `0257bc3ab36a63f55b0a8722fa54f7d5278a568941bdff7817934a891fcb0cbb`.
- Known limits: uses the 1.00 executable layout (the build stops with a log if a project hook does not match);
  the disc game folder or ISO is accepted, European update (PKG) data is not imported yet; the first preparation is slow.
