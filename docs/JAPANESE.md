# Versão japonesa / Japanese version (BLJS10206 1.30)

## Português

Suporte experimental ao executável japonês **BLJS10206 1.30** (`D2_GAME_VERSION=130`),
compilado localmente a partir dos seus próprios arquivos, como a versão USA. O launcher
gráfico e o modo ISO direto continuam apenas USA; use os comandos abaixo.

### Arquivos necessários (seus)

- Disco japonês BLJS10206 descriptografado, como pasta com `PS3_GAME/USRDIR`.
- As três atualizações de disco japonesas, todas obrigatórias (são incrementais):
  `JP0404-BLJS10206_00-…-A0110-…pkg` (1.10), `…-A0120-…pkg` (1.20) e `…-A0130-…pkg` (1.30).
- O EBOOT da 1.30 deve ter SHA-256
  `ee6139d5dfe4b56680e37ec01aa0ed01d2221547daa2e2504458992c41277e18`; o ELF resultante,
  `e30b3c7f83486a491f175be88642b778c0bcc3b2615272e7b44c072120026850`.

### Preparar e compilar

Mesmas ferramentas de [WINDOWS.md](WINDOWS.md), na raiz do repositório:

```bat
.venv\Scripts\python.exe tools\import_owned_pkg.py --hdd D:\MeusDados\dev_hdd0 --pkg 1.10.pkg --pkg 1.20.pkg --pkg 1.30.pkg
.venv\Scripts\python.exe tools\prepare_owned_self.py --self D:\MeusDados\dev_hdd0\game\BLJS10206\USRDIR\EBOOT.BIN --elf D:\MeusDados\EBOOT-130.elf
.venv\Scripts\python.exe tools\build_windows.py --version 130 --elf D:\MeusDados\EBOOT-130.elf --json-include D:\Dependencias\include
```

`import_owned_pkg.py` aplica as três atualizações em ordem em `game/BLJS10206` e cria
`game/NPJB00300`, a pasta de dados que o executável 1.30 consulta (cellGameDataCheck) para
ler os arquivos EDAT da 1.10. A compilação usa `port/build-windows-130`.

### Executar

Em `windows/config.local.json`, adicione `"version": "130"` e aponte `game_root` para o disco
japonês e `hdd0_root` para a pasta usada acima; depois execute `windows/iniciar_windows.bat`.

### O que muda em relação à USA 1.40

Os endereços específicos da 1.40 foram mapeados para a 1.30 por correspondência de assinaturas
de funções (instruções com imediatos de endereço/TOC mascarados). Cada função mapeada é idêntica
à da 1.40, exceto pelos endereços. Entrada/TOC `0x450FF8`/`0x46DF28`.

- Mapeados: fachada de vídeo, caminhos nativos de upload de vertex program e memset, correção
  de inicialização do SurMixer, ganchos de cheats, perfil do editor de itens.
- Cheats: perfil `1.30-JP` validado na memória (mesmos deslocamentos de grupo, classe, mana,
  movimento, salto, HL e itens que a USA 1.40 no mesmo ponto de um novo jogo).
- Sonda de warp de depuração (`D2_WARP_STAGE`): desativada na 1.30.
- Testado: abertura, novo jogo, cenas, pular eventos, primeiro mapa. O resto da campanha não foi
  validado.

Nenhum dado do jogo, atualização/DLC, firmware, chave, save ou código derivado do jogo é
distribuído.

## English

Experimental support for the Japanese **BLJS10206 1.30** executable (`D2_GAME_VERSION=130`),
compiled locally from your own files like the USA version. The GUI launcher and direct ISO mode
remain USA-only; use the commands below.

### Required files (your own)

- Decrypted Japanese BLJS10206 disc, as a folder with `PS3_GAME/USRDIR`.
- All three Japanese disc updates (they are incremental):
  `JP0404-BLJS10206_00-…-A0110-…pkg` (1.10), `…-A0120-…pkg` (1.20) and `…-A0130-…pkg` (1.30).
- The 1.30 EBOOT must have SHA-256
  `ee6139d5dfe4b56680e37ec01aa0ed01d2221547daa2e2504458992c41277e18`; the resulting ELF,
  `e30b3c7f83486a491f175be88642b778c0bcc3b2615272e7b44c072120026850`.

### Prepare and build

Same toolchain as [WINDOWS.md](WINDOWS.md), from the repository root:

```bat
.venv\Scripts\python.exe tools\import_owned_pkg.py --hdd D:\MyData\dev_hdd0 --pkg 1.10.pkg --pkg 1.20.pkg --pkg 1.30.pkg
.venv\Scripts\python.exe tools\prepare_owned_self.py --self D:\MyData\dev_hdd0\game\BLJS10206\USRDIR\EBOOT.BIN --elf D:\MyData\EBOOT-130.elf
.venv\Scripts\python.exe tools\build_windows.py --version 130 --elf D:\MyData\EBOOT-130.elf --json-include D:\Dependencies\include
```

`import_owned_pkg.py` layers the three updates in order into `game/BLJS10206` and creates
`game/NPJB00300`, the game-data folder the 1.30 executable checks (cellGameDataCheck) to read
the 1.10 EDAT files. The build goes to `port/build-windows-130`.

### Run

In `windows/config.local.json` add `"version": "130"`, point `game_root` at the Japanese disc and
`hdd0_root` at the folder used above, then run `windows/iniciar_windows.bat`.

### What differs from USA 1.40

The 1.40-specific addresses were mapped to 1.30 by function signature matching (instructions
with address/TOC immediates masked). Every mapped function is identical to 1.40 apart from
addresses. Entry/TOC `0x450FF8`/`0x46DF28`.

- Mapped: movie facade, native vertex-program upload and memset paths, SurMixer start-up fix,
  cheat hooks, item-editor profile.
- Cheats: `1.30-JP` profile verified in memory (same party, class, mana, move, jump, HL and item
  offsets as USA 1.40 at the same point of a new game).
- Debug warp probe (`D2_WARP_STAGE`): disabled on 1.30.
- Tested: boot, new game, story scenes, event skip, first map. The rest of the campaign is not
  validated.

No game data, updates/DLC, firmware, keys, saves or game-derived code are distributed.
