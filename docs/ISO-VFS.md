# VFS nativo para ISO — experimental / Native ISO VFS — experimental

Este desenvolvimento adiciona leitura direta de **UDF/ISO9660** em C++20, sem montar a
ISO no Windows e sem extrair assets. A integração fica desativada por padrão.
A versão publicada do launcher continua funcional com sua preparação existente.

## Implementado

- `IsoVfs::GetFileBytes`, `Open`, `File::ReadAt`, `Stat`, `ListDirectory` e `TitleId`.
- `CreateFileW` e leituras posicionadas Win32, com offsets de 64 bits e RAII.
- Handles persistentes, proteção de caminhos, limite de leitura integral e
  acesso serializado ao volume. O leitor UDF e o sistema operacional fornecem
  os buffers atuais; um cache LRU próprio e prefetch ainda não foram adicionados.
- Resolução de extents pelo libudfread 1.2.0 incorporado ao fonte.
- Alternativa ISO9660 própria, usada quando a montagem UDF falha, inclusive em
  imagens que têm os dois formatos. Lê o descritor primário com blocos de 2048
  bytes, diretórios, arquivos vazios e arquivos com várias seções/extents.
  Confere campos little/big-endian, tamanhos e limites físicos. Trata `;1` e
  versões de arquivo; usa comparação ASCII sem distinguir maiúsculas no
  backend ISO9660. O backend UDF mantém sua semântica original.
- Cache ISO9660 limitado a 32 diretórios; arquivos grandes continuam em streaming.
- Ponte de arquivos somente leitura para `/dev_bdvd` e `/app_home`.
- Adaptadores para PPU `cellFs`, syscalls LV2 `sys_fs` e a biblioteca direta
  `cellFs`, além de leitura AIO com preservação do cursor/callback existente.
- Montagens de `/dev_hdd0` continuam no backend existente, para atualização,
  DLCs e saves. Não houve mudança na preparação/importação desses conteúdos.
- Validação de `TITLE_ID=BLUS31313` antes do boot com ISO. A versão de código
  nativo continua precisando corresponder ao ELF/atualização utilizados.

Os adaptadores são cópias geradas na pasta de build. O SDK fixado no lock e seus
fontes originais não são editados; hashes diferentes interrompem a configuração.

## Verificação no Windows, sem ferramentas

O artefato `DisgaeaD2-ISO-Check-Windows-x64` da ação **Native ISO VFS** contém
`DisgaeaD2-ISO-Check.exe`. Abra-o para selecionar uma ISO **descriptografada**.
Ele lê o SFO, confere o título e verifica os 18 arquivos base, lendo até 4096
bytes de cada um. Salva um relatório `DisgaeaD2-ISO-diagnostico-*.txt` na pasta
Downloads e mostra o caminho ao terminar. É um
diagnóstico do leitor; **não executa o jogo** e não altera a ISO ou seus saves.
Também aceita um caminho de ISO na linha de comando.

A versão 2 inspeciona primeiro os descritores ISO9660/UDF e as âncoras UDF,
mesmo quando a montagem falha. Uma falha do leitor UDF **não comprova** que a
imagem está criptografada ou danificada. Se identificar ISO9660 sem UDF, registra
essa limitação do backend atual. Essa inspeção não adiciona suporte ISO9660 ao
runtime nem verifica criptografia de todos os assets. **A versão 3 adiciona o
backend ISO9660** e registra `Mounted backend=UDF` ou `Mounted backend=ISO9660`
no relatório, antes de ler o SFO e verificar os arquivos base.

## Compilar e testar o módulo (desenvolvimento)

```sh
cmake -S port/vfs -B build-vfs -DBUILD_TESTING=ON
cmake --build build-vfs
ctest --test-dir build-vfs --output-on-failure
```

`tools/check_iso_vfs.py` usa o SDK fixado e também executa os testes dos três
caminhos reais de filesystem. No Windows, utiliza o compilador portátil já
adotado pelo projeto; as ferramentas são de desenvolvimento, não do diagnóstico.

## Habilitar no runtime (desenvolvimento)

Adicione `-DD2_ENABLE_ISO_VFS=ON` à configuração CMake do port. Essa opção precisa
de uma build nova; o executável lançado anteriormente não tem este módulo.

```bat
DisgaeaD2Recomp.exe "C:\D2\work\v140\EBOOT.elf" --iso "F:\Jogos\DisgaeaD2.iso"
```

Alternativamente, defina `D2_GAME_ISO` com o caminho da ISO. As variáveis e
montagens atuais de HDD/firmware continuam necessárias nesta fase.

## Limites desta etapa

- Suporte inicial: imagens UDF/ISO9660 **descriptografadas**, compatíveis com os
  perfis implementados. O backend ISO9660 não interpreta nomes Joliet/Rock Ridge,
  arquivos intercalados ou volumes divididos em vários discos. Metadados legíveis
  não garantem que todos os setores de assets estejam descriptografados.
- Descriptografia de disco em tempo real, chaves fornecidas pelo usuário e
  importação nativa de PKG não estão implementadas neste módulo.
- AIO é integrado ao scaffold atual; serviços de streaming adicionais e
  carregamento de PRX por descriptor virtual precisam de validação específica.
- A compilação/recompilação local e o auxiliar de preparação da versão atual
  ainda não foram substituídos. **Ainda não é o produto final de um único EXE
  que recebe somente a ISO e joga.**
- Não altera gráficos, áudio, resolução interna, V-Sync ou frame pacing.
- Os testes usam UDFs criados do zero, incluindo arquivos fragmentados, Unicode,
  leitura física além de 4 GiB, concorrência, ABI big-endian e fallback HDD.
  A validação do jogo completo com uma ISO real continua necessária.

## English

This opt-in C++20 module reads assets directly from decrypted UDF/ISO9660 images using
native positioned I/O. It includes persistent handles, bounded whole-file
reads, directory access and a validated TITLE_ID parser. Generated adapters
connect the pinned SDK's PPU, LV2 and direct-library filesystem paths; AIO
preserves the existing callback behavior. HDD paths keep their existing backend.

The standalone Windows checker diagnoses an ISO and does **not** launch the
game. The current launcher release and preparation flow remain unchanged.
The version 3 checker includes a primary-descriptor ISO9660 fallback with
bounded directory caching, multi-extent streaming, endian/bounds validation
and ASCII case-insensitive lookup. It does not interpret Joliet/Rock Ridge,
interleaved files or multi-volume sets. The original UDF backend remains first.

Disc decryption, native PKG import, additional streaming
services and the final single-EXE product are not implemented by this milestone.
The game still requires matching locally generated code and its ELF. No game
data, firmware or lifted game code is distributed here.

## Third-party reader

The vendored libudfread is LGPL-2.1-or-later. Its original source files and
`COPYING` are preserved; see `port/vfs/vendor/libudfread/PROVENANCE.md` for the
official source archive and checksum. The Windows diagnostic artifact retains
object files and static libraries for relinking. Final runtime distribution
must preserve the applicable source, notices and relinking materials.

The in-project ISO9660 reader follows the primary descriptor and directory
record layouts in ECMA-119 (2019 edition, clauses 8 and 9):
https://www.ecma-international.org/wp-content/uploads/ECMA-119_4th_edition_june_2019.pdf
