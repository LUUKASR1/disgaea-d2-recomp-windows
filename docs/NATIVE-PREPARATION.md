# Preparação independente — standalone preview 0.1.13

O perfil `DisgaeaD2Recomp-Standalone` aceita a ISO descriptografada USA BLUS31313,
o PKG da atualização USA 1.40 e, opcionalmente, o pack DLC NPUB31321 já utilizado.
Não instala, inicia ou baixa RPCS3, firmware ou runtimes para esse auxiliar.
A interface pede somente a ISO e os PKGs locais. O VFS e o jogo são os mesmos
da etapa ISO validada; o launcher, Python e Crypto ficam dentro de um `.exe`.

## Implementação

- `tools/import_owned_pkg.py`: importador independente e limitado ao conteúdo
  suportado. AES-CTR para PKG PS3 retail e SHA-1 para debug, leitura posicionada,
  cópia em blocos de 1 MiB, limites de tabelas/arquivos e validação de caminhos.
  Recusa caminhos absolutos, traversal, nomes de dispositivos Windows, colisões,
  pacotes truncados, títulos incompatíveis e entradas não suportadas.
- Valida o TITLE_ID/APP_VER do SFO e o hash do SELF 1.40 antes de instalar uma
  atualização. O pack DLC precisa conter os flags NPUB31321 esperados; o importador
  não gera licenças/RAPs nem descriptografa ou modifica EDATs.
- Cada pacote é preparado em diretório temporário e publicado por rename.
  Registra hashes dos arquivos; uma repetição confere a instalação antes de
  reutilizá-la. Conteúdo existente diferente é preservado e causa erro.
- `tools/prepare_owned_self.py`: implementação independente e específica para
  o SELF 1.40 de licença gratuita, identificado por SHA-256 antes de ler os
  metadados criptografados. Reconstrói ELF64 PPC big-endian com verificações de
  limites, AES-CBC/CTR e inflate limitado. Não é um decrypter geral de SELF.
  O ELF resultante precisa ter o hash exato da build 1.40 validada.
- A pasta base permanece na ISO. Somente PARAM.SFO entra no cache de metadados;
  a atualização e os DLCs são importados para `hdd0`. Saves ficam nesse perfil.

## Verificação

O PKG local de referência produziu 33 arquivos idênticos, byte por byte, à
atualização previamente validada. O novo decrypter produziu exatamente o ELF
com SHA-256 `0ec183e580a270b0bb1b3a554794a9e4ec2d57a2be251ebdcf81fe21b78d225c`.
Nenhum desses arquivos, código recompilado ou dados de teste do jogo é incluído
no repositório/launcher. Os testes públicos usam somente fixtures sintéticas.

O CI verifica o parser/criptografia, erros, repetição, proteção de arquivos e
o caminho de preparação que proíbe o auxiliar/firmware. O mesmo teste de
criptografia roda dentro do executável empacotado, não apenas no Python do CI.
O teste completo de primeira preparação, DLCs, gameplay e save/load nesta
versão continua pendente no PC do usuário.

## Limites da distribuição

A primeira execução ainda baixa Git/LLVM portáteis privados e os fontes fixados
do SDK, gera o código da cópia do usuário e compila o runtime. Não há instalação
manual dessas ferramentas. Esta versão remove o emulador/firmware da preparação;
não promete primeira execução offline nem um runtime pré-compilado do jogo no
download. A versão 0.1.12 e seus saves permanecem no perfil anterior.

## Referências de formato

Implementações independentes, estudadas a partir dos layouts e constantes
públicas das seguintes fontes primárias; código RPCS3 não foi copiado para esses
módulos nem é distribuído/executado pelo caminho standalone:

- [RPCS3 SELF layout](https://github.com/RPCS3/rpcs3/blob/2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3/Crypto/unself.h)
- [RPCS3 metadata/ELF format reference](https://github.com/RPCS3/rpcs3/blob/2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3/Crypto/unself.cpp)
- [Public revision key constants](https://github.com/RPCS3/rpcs3/blob/2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3/Crypto/key_vault.cpp)
- [Public free-license constants](https://github.com/RPCS3/rpcs3/blob/2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3/Crypto/key_vault.h)
- [SDK PKG format reference](https://github.com/sp00nznet/ps3recomp/blob/a679051ef304555291de2eb3ec8a3dbf64a761a8/tools/pkg_extract.py)
- [PyCryptodome AES API](https://www.pycryptodome.org/src/cipher/aes)

English: this isolated preview removes RPCS3 and firmware from package/SELF
preparation. Only the supported owned free-license USA 1.40 executable is decoded;
the resulting ELF is verified against its exact known hash. Portable build tools
and SDK sources are still downloaded automatically on the first run. Real game
data and generated game code remain local and are never redistributed.
