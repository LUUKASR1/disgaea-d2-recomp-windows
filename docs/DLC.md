# DLC próprio, qualquer região / Owned DLC, any region

## Português

As versões japonesa, americana e europeia leem a mesma tabela de flags de DLC
(`USRDIR/Data/flag/flagGGGGIIII.edat`). Por isso, flags de DLC **que você comprou** em qualquer
região funcionam nas builds JP (`NPJB00300`), USA (`NPUB31321`) e EU (`NPEB01818`). O conteúdo
(personagens, itens etc.) já está nos dados do jogo; o flag apenas o libera.

`tools/import_owned_dlc.py` não contém chaves e não descriptografa nada. Ele copia seus flags
para a pasta da build escolhida e cria o `PARAM.SFO` dessa pasta, se necessário.

### O que você precisa

1. **Seus arquivos de DLC**: a pasta instalada do seu PS3 ou RPCS3, por exemplo
   `dev_hdd0/game/NPJB00300` (contendo `USRDIR/Data/flag/flag*.edat`).
2. **Para DLC pago (licença tipo 1/2)**: a sua própria chave RIF de 16 bytes para cada content ID,
   como `<CONTENT_ID>.rifkey` (por exemplo `JP0404-NPJB00300_00-DISGAEAD20000001.rifkey`). Ela
   vem da sua licença (`.rap`/`.rif`); este projeto não converte licenças. DLC gratuito (licença
   tipo 3) não precisa de chave.

### Instalar

```bat
.venv\Scripts\python.exe tools\import_owned_dlc.py --target us --hdd D:\MeusDados\dev_hdd0 --dlc D:\MeuPS3\dev_hdd0\game\NPJB00300 --keys D:\MinhasChaves
```

`--target` é `jp`, `us` ou `eu`; `--hdd` é a pasta `dev_hdd0` usada por essa build. Use `--check`
para só ver o que seria instalado. Flags pagos sem chave são listados e não são instalados.

### Conferir

Inicie o jogo e procure no log: `[NPDRM] accepted …flag…` (liberado) ou `[NPDRM] rejected` (chave
errada ou de outra licença).

Use apenas DLC e licenças suas e **nunca compartilhe arquivos `.rifkey`**: a chave é a mesma para
todos os compradores daquele conteúdo.

## English

The Japanese, USA and European releases read the same DLC flag table
(`USRDIR/Data/flag/flagGGGGIIII.edat`). DLC flags **you bought** in any region therefore work in
the JP (`NPJB00300`), USA (`NPUB31321`) and EU (`NPEB01818`) builds. The content (characters,
items, etc.) already ships in the game data; the flag only unlocks it.

`tools/import_owned_dlc.py` contains no keys and decrypts nothing. It copies your flags into the
chosen build's folder and creates that folder's `PARAM.SFO` when needed.

### What you need

1. **Your DLC files**: the installed folder from your PS3 or RPCS3, e.g. `dev_hdd0/game/NPJB00300`
   (containing `USRDIR/Data/flag/flag*.edat`).
2. **For paid DLC (license type 1/2)**: your own 16-byte RIF key for each content ID, as
   `<CONTENT_ID>.rifkey` (for example `JP0404-NPJB00300_00-DISGAEAD20000001.rifkey`). It comes from
   your license (`.rap`/`.rif`); this project does not convert licenses. Free DLC (license type 3)
   needs no key.

### Install

```bat
.venv\Scripts\python.exe tools\import_owned_dlc.py --target us --hdd D:\MyData\dev_hdd0 --dlc D:\MyPS3\dev_hdd0\game\NPJB00300 --keys D:\MyKeys
```

`--target` is `jp`, `us` or `eu`; `--hdd` is the `dev_hdd0` folder that build uses. `--check`
only reports what would be installed. Paid flags without a key are listed and skipped.

### Verify

Start the game and look in the log for `[NPDRM] accepted …flag…` (unlocked) or
`[NPDRM] rejected` (wrong key, or a key from another license).

Use only your own DLC and licenses and **never share `.rifkey` files**: the key is the same for
every buyer of that content.
