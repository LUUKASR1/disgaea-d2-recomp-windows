# Disgaea D2 — Android ARM64 bootstrap 0.0.14

## Português

### Pacote integrado da 0.0.14 — segmentos vazios e aliases SELF

Corrige o bloqueio identificado no relatório real da 0.0.13: os programas 2, 3
e 4 de BLUS31313 declaram zero bytes em arquivo e memória, mas tinham offsets
não congruentes com endereço zero. O plano agora dispensa congruência apenas
quando `p_memsz == 0`; `p_filesz <= p_memsz`, limites de arquivo/endereço e
alinhamento em potência de dois continuam obrigatórios. LOADs vazios não contam
como regiões carregadas. O mesmo tratamento vale para ELF direto e para a
visão ELF dentro de SELF. Espere `self_empty_load_segments_ignored=3` e dois
LOADs não vazios se o restante da imagem passar.

Também substitui a rejeição geral de sobreposição lógica por uma verificação
limitada. Cabeçalhos dentro do primeiro LOAD e registros TLS/processo dentro de
um LOAD podem descrever bytes compartilhados. Cada par sobreposto é conferido,
incluindo regiões aninhadas não adjacentes:

- O mesmo intervalo físico do mesmo arquivo é um alias e não precisa ser relido.
- Cópias em locais distintos, ou em buffers zlib, precisam coincidir byte a byte.
- Conteúdo diferente produz `self-plain-overlap-content-mismatch`; nenhum lado
  é escolhido arbitrariamente. Os índices referem-se às regiões da visão
  ordenada (incluindo cabeçalhos), não aos índices da tabela de programas.
- As comparações usam blocos de 4 KiB, até 4 MiB de bytes comparados. Excesso
  produz `self-overlap-validation-budget`. Os limites de I/O da montagem e de
  descompressão anteriores continuam em vigor. Sobreposição na memória guest
  entre LOADs não vazios continua rejeitada.

Os campos `self_overlap_alias_regions`, `self_overlap_compared_regions` e
`self_overlap_compared_bytes` descrevem a auditoria; `self_overlap_validation=passed`
aprova somente esse passo. `self-plain-plan-valid` ainda significa plano
estrutural limitado, sem VM guest, decodificação retail, dispatch ou gameplay.

O pacote integrado acrescenta `disc_guest_self_empty_and_overlap_plan=passed`
e `disc_guest_self_overlap_conflict_rejected=passed`. Usa oito programas
sintéticos com a topologia observada: dois LOADs, três placeholders vazios,
TLS e dois registros de processo, com cabeçalhos dentro do primeiro LOAD.
Nenhum byte do jogo foi copiado. Os 69 casos SELF locais cobrem placeholders,
alinhamento de BSS, sobreposição idêntica/divergente, regiões aninhadas,
zlib, teto de comparação e os casos anteriores. A comparação opcional com o
CLI anterior continua conferindo os 52 casos antigos; os novos layouts têm
mudança intencional de aceitação.

**O jogo ainda não inicia.** A correção passou em fixtures próprias; a ISO real
continua dependendo de verificação no Odin. Selecione a mesma ISO e envie o
relatório. Não precisa extrair ou modificar o jogo.

**English:** 0.0.14 fixes zero-memory LOAD congruence handling in direct ELF and
SELF plans while retaining file-size, bounds and power-of-two checks. Empty
LOADs are excluded from loaded-region counts. Logical overlaps are accepted
only for the same backing bytes or bounded byte-identical copies, including
zlib buffers and non-adjacent nested regions. Conflicts and comparison-budget
excess are rejected; nonempty guest LOAD overlaps remain invalid. A new
on-device eight-program synthetic layout and 69 host cases cover both fixes.
No game bytes are bundled; real-device image planning remains unverified and
game boot/runtime integration is still unavailable.

### Pacote integrado da 0.0.13 — causa exata da rejeição SELF

O relatório real da 0.0.12 chegou a SELF v2/revisão 28/tipo de aplicação 4, mas
parou em `self-load-segment-bounds-or-alignment`. Esse código reunia seis
condições e não continha os campos do segmento; não permite determinar qual
falhou nem concluir que a ISO está inválida.

A 0.0.13 mantém os critérios de aceitação da 0.0.12 e melhora o diagnóstico:

- Registra todos os até 128 programas da tabela pública já limitada, antes de
  rejeitar um segmento. Cada linha `self_segment_N` contém somente números:
  tipo/flags, offset ELF, endereço virtual, tamanhos em arquivo/memória,
  alinhamento e restos da divisão, offset/tamanho no SELF, compressão/encriptação.
- Informa contagens declaradas de compressão/criptografia mesmo quando um
  segmento é rejeitado. Essas declarações não autenticam nem decodificam dados.
- `self_failure_segment_index` identifica o programa com falha (índice começa
  em zero). `self_failure_checks` lista todas as condições LOAD que falharam
  nesse programa: `filesz-exceeds-memsz`, `address-width`, `address-range`,
  `segment-budget`, `alignment-not-power-of-two`, `offset-address-incongruent`.
- `self_failure_class=inspection-budget` distingue o teto local de 256 MiB por
  segmento das outras verificações (`load-layout`). Essa classificação não
  afirma que o restante do executável seja válido. `executable_reason` aponta
  a primeira condição; o grupo antigo fica em `self_failure_group`.
- Limites lógicos/físicos e flags inválidos também recebem motivos separados.
  Não são registrados caminhos, conteúdo do jogo, identificadores ou chaves.

O teste integrado novo, `disc_guest_self_failure_details=passed`, reproduz uma
incongruência sintética e verifica a causa, o índice e a tabela completa. Os
52 cenários SELF locais incluem cada condição, falhas simultâneas, rejeição no
segundo programa, valores exatos e recuperação. Uma comparação opcional com o
CLI anterior confirma que os mesmos cenários continuam aceitos/rejeitados.
O teste UI espera as janelas do seletor e limita os comandos de voltar; a
confirmação de cancelamento continua obrigatória no relatório real do app.

**Ainda não corrige a causa do segmento real nem inicia o jogo.** Precisamos
dos campos que a 0.0.12 não coletou. Instale, aguarde os testes, selecione a
mesma ISO e compartilhe o relatório. Não precisa trocar ou extrair a ISO.

Referência dos critérios ELF: [System V gABI, Program Header](https://gabi.xinuos.com/elf/08-pheader.html).
Nenhum critério de alinhamento foi relaxado nesta etapa.

**English:** 0.0.13 retains 0.0.12 acceptance rules while reporting bounded,
public numeric program/segment fields before rejection. LOAD failures include
index, all failed checks, exact first reason and a separate inspection-budget
classification. It does not fix the unknown real-image failure, decrypt retail
SELF, map guest memory or boot the game. The integrated synthetic diagnostic
check and 52 host cases cover exact causes and recovery; the optional old/new
CLI comparison checks acceptance parity. Picker cancellation waits for visible
windows with bounded Back retries while still asserting the real cancel result.

### Pacote integrado da 0.0.12 — cabeçalhos SELF e segmentos plain/zlib

Substitui a identificação por assinatura da 0.0.11 por um parser independente
limitado dos cabeçalhos públicos SCE v2 / SELF ext v3 / ELF64 PPC64 big-endian.
Valida limites de tabelas, contagem de programas, regiões dos segmentos,
alinhamento, endereços guest, sobreposição dos PT_LOAD e cadeia de controles.
Informa tipo de aplicação, revisão, tipo numérico de licença NPD (quando existe)
e contagens declaradas de segmentos criptografados/comprimidos. Não registra
conteúdo, caminhos, IDs de licença nem chaves. As assinaturas não são verificadas;
`self_public_headers=structurally-valid` cobre somente este subconjunto público.

Se qualquer segmento declara criptografia solicitada (`encryption=2`), informa
`executable_status=self-decryption-required` e para antes de ler seu conteúdo.
O flag sozinho não autentica a criptografia. Perfis de cabeçalho fora do
subconjunto têm `self-profile-unsupported`; estruturas inválidas são rejeitadas
com motivo fixo. O decoder Python do Windows é exclusivo da atualização USA 1.40
e não foi ligado ao Android nem generalizado para o executável-base da ISO.

Para segmentos que declaram formato plain, sem criptografia solicitada,
constrói uma visão ELF somente para leitura, sem extrair arquivos. Segmentos
zlib usam a biblioteca de sistema com stream completo e tamanho exato; rejeita
truncamento, bytes extras, expansão excessiva e tamanho divergente. Limites:
16 MiB por entrada/saída e 24 MiB acumulados de entrada+saída zlib, além do
orçamento de I/O por montagem já existente. Regiões ELF lógicas sobrepostas
(inclusive segmentos que englobam cabeçalho/tabela) são um layout não suportado
e são rejeitadas. Não se trata de um decoder SELF geral.

`executable_status=self-plain-plan-valid` significa que a visão plain/zlib
passou no mesmo plano limitado PT_LOAD/OPD/código/TOC da 0.0.11. Não escreve
memória guest, não executa PPC e não valida TLS, importações ou dependências.
O teste integrado do APK cobre SELF sintético plain, gate de criptografia,
zlib válido/inválido, tabela inválida e recuperação; os testes anteriores
continuam no mesmo pacote. Espere os quatro campos `disc_guest_self_*` aprovados.

**O jogo ainda não inicia.** Esta etapa prepara e valida a visão de leitura;
carregador completo, decodificação retail, runtime jogável, RSX e áudio ainda
estão pendentes. Selecione a mesma ISO e compartilhe o relatório para identificar
qual perfil público o executável-base realmente apresenta.

Referência de formato: [layout SELF público do RPCS3, revisão fixada](https://github.com/RPCS3/rpcs3/blob/2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3/Crypto/unself.h).
A implementação deste parser é independente; nenhum código RPCS3 foi copiado.

**English:** 0.0.12 adds an independent bounded SCE/SELF public-header parser and
plain/zlib segment-backed ELF planning. Encryption-requested segments are gated
before reading their payload; no keys or retail decryption are implemented.
System zlib uses exact stream/input/output checks and strict memory caps.
The virtual ELF plan remains read-only, with no guest writes or game execution.
Overlapping logical layouts and unsupported profiles are rejected. Four new
on-device synthetic checks cover plain, encrypted gating, zlib and recovery.
Public-header validity is partial, unsigned and is not proof of a playable game.

### Pacote integrado da 0.0.11 — serviços de disco e plano do executável

Liga o leitor UDF/ISO9660 aos seis adaptadores LV2 de arquivos. Caminhos
`/dev_bdvd/...` agora podem abrir arquivos internos da imagem e usar
`read/lseek/fstat/close` com handles e cursores independentes. Escritas são
recusadas. Esta sessão diagnóstica mantém os limites de 32 MiB/8192 operações
físicas por montagem, oito handles e 64 KiB por transferência. Ainda não é
uma montagem persistente para uma sessão de jogo.

O teste automático cria uma pequena ISO9660 sintética privada e acessa arquivos
por meio da função PPC de syscall traduzida pelo lifter, verificando bytes,
metadados big-endian, EOF, arquivo vazio, caminhos inválidos, diretórios,
remontagem com handles ativos, recuperação após erros e limpeza dos descritores.
Espere `disc_guest_status=passed`, `disc_guest_failures=0` e
`dispatch_integrated_disc=passed`. O teste usa somente dados criados pelo projeto.

Ao selecionar sua ISO, o relatório também inspeciona o `EBOOT.BIN` sem executá-lo.
`executable_status=self-loader-required` identifica a assinatura SELF, sem
concluir se o conteúdo está criptografado ou se o container é válido.
Para ELF64 PPC64 big-endian, um plano limitado verifica tabela de segmentos,
limites do arquivo e endereços, alinhamento, sobreposição, orçamento de memória
e entrada OPD/código/TOC. `executable_status=elf-plan-valid` aprova somente essas
verificações estruturais: não valida todos os metadados ELF, TLS, importações,
funções recompiladas ou dependências. Nenhuma memória guest é alocada/escrita
por essa inspeção. O seletor usa uma montagem local isolada do teste guest,
portanto não disputa sua VM nem seus handles.

**O jogo ainda não inicia.** O runtime completo, carregador SELF, dados/código
recompilado do jogo, SPU completo, renderização RSX e áudio continuam pendentes.
Instale esta versão, aguarde o teste automático, selecione a mesma ISO e use
**Compartilhar relatório**. Não é necessário repetir testes de ciclo de vida
ou controles para esta etapa.

**English:** 0.0.11 connects bounded descriptor-backed disc sessions to the six
read-only LV2 file adapters. An internally generated ISO9660 is exercised via
the genuinely lifted synthetic syscall entry; independent file handles/cursors,
bytes/stat/seek/EOF, errors, mount transactions and teardown are checked. Expect
`disc_guest_status=passed` and zero failures. The user-selected ISO separately
gets bounded EBOOT signature inspection and an ELF64 BE PPC64 segment/OPD plan.
SELF magic is reported as `self-loader-required`, not evidence of encryption.
An accepted ELF plan is structural only: there is no guest mapping, decryption,
game code execution or complete ELF/TLS/import validation. Game boot remains
unavailable. Select the same ISO and share the diagnostic report.

### Pacote integrado da 0.0.10 — inspeção ISO/VFS

Reutiliza o leitor UDF/ISO9660 do Windows com uma nova entrada por descritor POSIX
(e fatia do provedor Android). O arquivo é aberto somente para leitura; não é
extraído. Ao selecionar uma ISO reconhecida, tenta montar o volume temporariamente,
valida a tabela de `PARAM.SFO`, identifica `TITLE_ID` e verifica por amostragem os
18 arquivos-base esperados de BLUS31313. Limite de I/O da montagem por descritor:
32 MiB e 8192 chamadas; não é uma auditoria completa do conteúdo da imagem.

Espere `disc_status=base-files-readable`, `disc_required_files_checked=18` e
`disc_required_files_failed=0` para uma imagem compatível com todos os arquivos.
Outro título, arquivo-base ausente/ilegível ou imagem não montada têm estados
separados. `document_status=readable` sozinho continua significando apenas que o
arquivo selecionado pode ser lido. O relatório não contém nome/URI nem conteúdo
dos assets. **Não valida descriptografia dos assets nem inicializa o jogo**.
A montagem é exclusiva desta inspeção; ainda não está ligada ao runtime guest.
Atualizações e DLCs não fazem parte dos 18 arquivos-base verificados.

`libudfread` é distribuída como biblioteca compartilhada separada, com licença
LGPL-2.1-or-later no APK. As fontes vendorizadas permanecem sem alterações; a
entrada de descritor e integração CMake ficam no código deste projeto.

**English:** 0.0.10 reuses the desktop UDF/ISO9660 reader with Android descriptor
slices. It temporarily mounts recognized images read-only, parses bounded SFO
metadata and samples the 18 expected BLUS31313 base files. Descriptor mounts have
a 32 MiB / 8192-call read budget. Expect `disc_status=base-files-readable` and 18
successful checks for a compatible complete base image. This is not asset
decryption validation, a guest runtime mount or game boot. No extraction is
performed; updates/DLCs are separate. LGPL libudfread is a separate shared library
with its license bundled; the vendored upstream sources are unchanged.

### Pacote integrado da 0.0.9 — seletor de arquivos Android

O botão **Select file to validate** abre o seletor de documentos do Android.
A abertura é somente para leitura e ocorre fora da thread da interface; não
solicita acesso geral ao armazenamento nem guarda a URI ou seu nome no relatório.
O descritor é passado ao mesmo leitor C++ limitado da 0.0.8, respeitando a fatia
`startOffset/declaredLength` do provedor. Descritores por pipe/stream são recusados.

São lidos no máximo 64 KiB do início e 64 bytes do final. O relatório registra
tamanho, teste de limites e uma indicação de cabeçalho ELF/SELF/SFO/ISO/UDF,
quando encontrada. **A indicação não valida o jogo nem o formato completo**.
Não há montagem PS3, parser ISO/UDF completo ou execução de código do arquivo.
Cancelamento, arquivo vazio, falha de permissão/leitura e retorno do seletor
produzem estados distintos; a seleção pode ser repetida. Os descritores são
fechados após cada teste, sem cópia integral do arquivo.

Instale o APK diagnóstico, aguarde os testes automáticos, selecione um arquivo
local (pode ser sua ISO ou um arquivo de teste) e compartilhe o relatório.
Espere `document_status=readable` e `document_boundary_check=passed` para um
arquivo regular não vazio. Isso ainda **não inicia Disgaea D2**.

**English:** 0.0.9 integrates Android's document picker with the bounded native
reader. It uses read-only document descriptors, samples at most 64 KiB at the
start and 64 bytes at the end, reports size/bounds and header hints, and handles
cancellation, empty files and provider failures. Filenames, URIs and contents
are excluded from the shared report. Header hints are not complete format or
game validation. No PS3 mount, ISO/UDF parser or game execution is enabled.
Select a local file, then share the diagnostic report.

### Pacote integrado da 0.0.8 — arquivos

Acrescenta um leitor C++ de fatias limitadas de um descritor de arquivo regular
(e duplicado), sem exigir caminho de armazenamento externo. Seis adaptadores
LV2 novos ligam `open/read/write/close/lseek/fstat` ao caminho `sc` do fixture
recompilado pelo lifter real: escrita é recusada, os demais operam somente sobre
um arquivo virtual explicitamente registrado e somente para leitura.
Os handlers completos `sys_fs`/`cellFs` do SDK não estão ligados nesta etapa.

O mesmo APK verifica limites e overflow, leituras paralelas com `pread`, cursor
independente por handle, EOF, metadados big-endian, rejeição de caminhos inválidos,
ponteiros completos antes de conversão, limite de handles, handles obsoletos,
truncamento externo sem escrita parcial na memória guest e limpeza dos recursos.
Transferências são limitadas a 64 KiB, seeks ficam dentro da fatia, metadados de
proprietário e tempo são sintéticos; nenhum host path arbitrário é aceito.

Espere `files_status=passed` e `files_failures=0`, junto aos testes anteriores.
Ainda usa **arquivo sintético interno**: não há seletor de documentos Android,
parser ISO/UDF, saves graváveis ou inicialização do jogo. O leitor é a base para
um futuro descritor obtido via SAF; provedores por pipe/stream são rejeitados.

**English:** 0.0.8 adds a bounded read-only descriptor slice and six new LV2
adapters exercised through the real lifted synthetic syscall entry. The grouped
tests cover parallel positional reads, independent cursors, endian stat output,
EOF, bounds/overflow, invalid guest pointers, handles, truncation and cleanup,
alongside all 0.0.7 tests. This is not the SDK's full filesystem, a document
picker, ISO/UDF parsing, writable saves or game boot. Expect `files_status=passed`
and `files_failures=0`. No game files or storage permissions are needed.

### Pacote integrado da 0.0.7

Uma instalação e um relatório agora cobrem núcleo, ELF, código recompilado,
relógios e um teste integrado de memória/threads/sincronização. O worker PPC
adicional passa pelo lifter e executa em quatro threads criadas pelo verdadeiro
`sys_ppu_thread_create` do SDK. O ciclo é repetido três vezes: **12 workers**.

O teste aloca e verifica memória zerada, usa mutex e semáforos do SDK, incrementa
um contador compartilhado, recebe sinais de conclusão e chama o verdadeiro
`sys_ppu_thread_exit` seguido de `sys_ppu_thread_join`. Uma escrita após a saída
precisa permanecer sem executar. Também são verificados timeout de mutex em
outra thread, deadlock recursivo rejeitado, limite/timeout de semáforo, erros de
handles, liberação/contabilidade, rejeição de acesso após free e auditoria final
de recursos. A admissão dos workers é serializada por um semáforo; isto não é
um stress test geral de todas as corridas possíveis do SDK.

Mapeamento/proteção são testados pelas primitivas reais `vm_commit`/`vm_protect`,
com rastreamento de permissões e descarte de páginas no host. O free do SDK
sozinho é contábil; a ponte adicional revoga acesso e descarta as páginas. Os
caminhos incompletos de `sys_mmapper` que apenas confirmam sucesso não ficam
acessíveis. A ponte admite apenas as formas de chamada usadas pela fixture,
limita tamanhos e timeouts, valida ponteiros completos antes de estreitá-los
e não aceita imagens ou workloads arbitrários.

A cópia preparada de `sys_ppu_thread.c` exclui dois hooks fora deste teste:
reservas cruzadas PPU e handlers de interrupção raw-SPU. O worker fixo não contém
lwarx/stwcx; essas duas capacidades continuam marcadas como indisponíveis, sem
handlers fictícios. O hook padrão de conclusão SPU do SDK permanece sem jobs
registrados. A sessão só reabre o registro de workers quando os anteriores já
terminaram e foram aguardados. Um watchdog encerra o processo se não puder
comprovar a conclusão de um worker; nunca libera a VM sob uma thread viva.
O caminho que atravessa o longjmp do SDK não contém objetos C++ com destrutores.

Esperado: **`bundle_status=passed`**, **`bundle_failures=0`**,
`bundle_rounds=3`, `bundle_workers_created_and_joined=12`, além dos testes
anteriores aprovados. Ainda não há boot de Disgaea, scheduler SPU completo,
áudio ou renderer do jogo. Não é necessário fornecer ISO nesta etapa.

### Serviços de relógio introduzidos na 0.0.6

O teste agora tem dez funções PPC sintéticas traduzidas pelo lifter. As novas
funções chamam duas syscalls reais de horário do SDK (145 e 147) e uma importação
HLE `cellRtcGetCurrentTick`. A ponte usa a tabela LV2 e a tabela NID do SDK;
o NID calculado no build é comparado com o calculado pelo próprio SDK em execução.
`sys_timer.c` e `cellRtc.c` são compilados sem alterar seus bytes, com seções
individuais: o linker mantém somente os handlers selecionados. Não se habilitam
threads de timers, filas de eventos ou outros serviços dessas bibliotecas.

Antes dos handlers receberem ponteiros, a ponte verifica a faixa inteira, a
largura de 64 bits e o alinhamento. Os testes comparam o horário com o relógio
do host, verificam epoch/unidade do RTC e saída big-endian. NULL no RTC retorna
o `CELL_EINVAL` real. Syscalls/NIDs desconhecidos retornam `CELL_ENOSYS` e
marcam a chamada como não suportada; não simulam sucesso. O contexto gerado é
copiado para o contexto do SDK depois de validar a ABI, sem aliasing entre tipos.

Esperado: **`services_status=passed`**, **`services_failures=0`**,
`dispatch_status=passed` e `dispatch_generated_functions=10`.
Ainda são fixtures privadas. Não há inicialização de Disgaea, áudio, VFS do jogo,
scheduler PPU/SPU completo ou ponte genérica para todas as syscalls/HLE.

### Execução sintética introduzida na 0.0.5

Seis funções PowerPC escritas para este teste são traduzidas pelo próprio
`ppu_lifter.py` do SDK fixado e compiladas em código ARM64 durante o build.
Não são funções do jogo nem C++ escrito à mão para fingir uma recompilação.
A imagem ELF, a lista de funções e o código gerado têm hashes de proveniência.
O aplicativo não distribui ou executa o lifter Python no aparelho.

Um worker único executa núcleo, ELF e despacho em sequência; cada etapa libera
sua VM antes da próxima. O teste de despacho carrega seu ELF sintético, resolve
OPD/TOC, registra as funções usando o registro hash extraído literalmente do SDK
e executa o C++ gerado. A ponte de chamadas deste teste é limitada: não é o
`ps3_indirect_call`/`ppu_run` completo do SDK. Ela testa soma com leitura via TOC,
store/load big-endian, chamada direta, chamada indireta e tail trampoline.
Também compara tamanho, alinhamento e offsets do contexto gerado com o contexto
real do SDK, verifica colisão/substituição no registro, rejeita endereços fora
da memória e funções ausentes, e separa erros/contextos entre threads.

Uma função sintética com `sc` deve retornar **erro de syscall indisponível**:
o teste só passa se a operação não fingir sucesso. Nenhum HLE, áudio, RSX do
jogo, scheduler completo ou função de Disgaea é iniciado. Os limites da ponte
não interrompem loops arbitrários de código gerado: só as seis funções fixas
são admitidas neste teste privado.

Aguarde cerca de 10 segundos e compartilhe o relatório. Esperado:
`dispatch_status=passed`, `dispatch_failures=0`,
`dispatch_generated_functions=6`, `dispatch_lifted_code_executed=true` e
`dispatch_game_executed=false`. Não selecione ISO ou arquivos do jogo.

### Teste do subconjunto ELF mantido da 0.0.4

Depois de o núcleo liberar a VM, um único worker cria uma VM nova e carrega
somente um ELF sintético criado no diretório privado do aplicativo. O parser
ELF64 big-endian PPC64 e a cópia de segmentos vêm literalmente do objeto Git
fixado do SDK. A extração verifica o hash do trecho e termina antes dos hooks
de depuração que iniciam threads. Não é o loader PPU completo.

São verificados PT_LOAD, BSS zerado, leitura de entrada OPD/TOC, metadados de
PT_TLS, recarga válida e limpeza de memória/arquivo temporário. Vinte e nove
fixtures inválidas ou truncadas devem ser rejeitadas sem alterar a área guest
monitorada de 5 MiB nem os metadados TLS. A faixa aceita por esse teste é
inteiramente comprometida antes da cópia. Nenhum código PPC é executado.

O relatório deve ter **`core_status=passed`**, **`elf_status=passed`**,
**`elf_failures=0`** e **`elf_rejected_fixtures=29`**. A reserva virtual é usada
sequencialmente, sem somar duas VMs. O teste não recebe ISO, ELF do usuário,
atualizações ou DLCs. O dispatcher, a inicialização de TLS por thread, o HLE,
áudio e execução do jogo continuam pendentes. Antes de aceitar arquivos do
usuário, este subconjunto precisa de uma interface limitada de leitura e de
planejamento/commit dos segmentos; não exponha `ppu_load_elf` diretamente.

### Teste do núcleo mantido da 0.0.3

Esta etapa liga **componentes reais do SDK**, não o runtime completo do jogo.
O diagnóstico roda uma única vez em uma thread de fundo, sem bloquear a interface:

- `vm_init` reserva 4 GiB de **espaço virtual**, com alinhamento de 4 GiB.
  Isso não significa consumir 4 GiB de RAM. A inicialização existente toca cerca
  de 256 MiB de memória principal; o teste libera a reserva ao terminar.
- Acessos big-endian, página no topo do espaço de endereços e alteração de proteção.
- Operações inteiras PPU e o caminho inline de reserva/CAS, incluindo falha após
  alteração do valor. O registro completo de reservas do loader e casos ABA não
  estão validados por esse teste.
- Quatro threads do shim do SDK fazem 20.000 incrementos no total usando o CAS
  real em memória guest; também são verificados eventos e semáforo.
- As próprias fixtures de instruções e operações numéricas SPU do SDK são
  compiladas no APK. ARM64 usa os caminhos NEON que o SDK já fornece.

O relatório deve incluir `core_status=passed`, `core_failures=0` e os resultados
individuais `core_...`. Aguarde cerca de 10 segundos antes de compartilhar.
Um resultado positivo não valida o scheduler PPU/SPU completo, syscalls/HLE,
reservas entre PPU e contextos SPU, ELF loader, saves, áudio ou execução do jogo.
Os hooks opcionais de debug/loader ficam desativados e abortam se acionados,
em vez de simular uma implementação ausente.

As fontes são extraídas do objeto Git fixado em `patches/SDK-WINDOWS.lock`, com
hashes de proveniência. Nenhum checkout SDK do usuário é editado, e nenhuma
fonte gerada a partir do jogo é lida. O APK inclui os avisos de licença do SDK.
Somente as cópias de build recebem as adaptações Android: workers registrados
param cooperativamente em pontos de polling, e afinidade usa o ID de thread
do Android com `sched_setaffinity`. O teste espera um worker estar realmente
bloqueado no poll, solicita a parada e verifica sua saída e a limpeza do waiter.
Outros waits de HLE e parada do scheduler completo ainda não estão validados.
Nenhuma afinidade é aplicada pelo probe; a política de CPU permanece com o SO.
O probe é dono da VM neste aplicativo: antes de ligar um loader de jogo, ele
precisa ser removido desse caminho ou isolado em outro processo. Não pode rodar
simultaneamente sobre a VM de uma sessão de jogo.

### Correção da 0.0.2

O print do teste no Odin confirmou o triângulo, mas o painel de diagnóstico não
apareceu na 0.0.1. NativeActivity entrega a superfície principal à renderização
nativa; widgets comuns adicionados a essa janela foram encobertos pelos presents.
A 0.0.2 usa uma janela de painel separada, sem roubar foco dos controles. Ela
some ao minimizar e reaparece ao retornar. O CI também testa a interface real em
um emulador Android, incluindo o clique para abrir o compartilhamento.

Se o Android recusar a atualização por diferença de assinatura, desinstale
**somente o aplicativo de diagnóstico anterior** e instale este APK. As chaves de
debug do CI não são persistentes. Este app ainda não contém saves nem arquivos
do jogo.

**APK de diagnóstico; ainda não executa o jogo.** Primeiro passo do port para
Android, com foco inicial no AYN Odin 2 Portal. Não altera a versão Windows.

O app reaproveita o renderer Vulkan experimental do projeto, agora com
`ANativeWindow`, `VK_KHR_android_surface` e o loader Android `libvulkan.so`.
Usa NativeActivity e a thread de eventos do NDK; libera a superfície antes de
o Android destruir a janela e pausa a renderização ao perder foco ou minimizar.

### Teste no aparelho

1. Baixe o APK `DisgaeaD2-Android-0.0.10-Disc-VFS-Test-arm64.apk` e instale no Odin.
2. Abra **D2 Android • Teste do núcleo**. Deve aparecer um triângulo colorido e um
   aviso de que o app ainda não executa Disgaea D2.
3. Aperte os botões físicos e direcional; mova os analógicos. O app conta eventos
   de botões, do direcional e do eixo esquerdo para diagnóstico, sem ações de jogo.
4. Vá à tela inicial do Android e volte ao app duas vezes. Bloqueie e desbloqueie
   a tela. Confira se a imagem retorna e se há travamento ou fechamento.
5. Toque em **Compartilhar relatório** e envie o texto junto com seu relato do
   que apareceu. O relatório inclui GPU/API Vulkan, quadros do teste, eventos de
   janela e controles. Não inclui arquivos pessoais, caminhos locais ou ROM.

Não precisa selecionar ISO, instalar firmware ou conceder permissão de arquivos.
Requer Android 9+ ARM64 e Vulkan 1.1. O requisito declarado não garante todos os
drivers; o teste no hardware real está pendente. Esta build é assinada com chave
de debug, sem promessa de compatibilidade de atualização entre builds de teste.

### O que ainda falta para jogar

- Ligar o fluxo RSX completo ao Vulkan. O Vulkan de desktop também está em
  desenvolvimento; apresentar um triângulo não valida a renderização do jogo.
- Compilar e validar o runtime PPU/SPU e o código recompilado para Android ARM64,
  incluindo memória virtual, threads, sincronização e instruções específicas de CPU.
- Entrada Android para os comandos reais do jogo e áudio nativo.
- Integração de arquivos por seletor de documentos/descritores, VFS, update/DLC e
  armazenamento de saves. URIs do Android não são caminhos comuns de Windows.
- Estratégia de preparação local dos dados fornecidos pelo usuário, desempenho,
  temperatura e validação de campanha no aparelho.

Não são distribuídos jogo, ISO, update/DLC, firmware, chaves, saves ou código
recompilado do jogo. O APK não é um emulador nem contém o runtime jogável neste
estágio; é um teste isolado da plataforma gráfica e dos eventos Android.

### Compilar

Requer JDK 17, Gradle 8.11.1, Android SDK 35 / Build Tools 35.0.0, NDK
28.2.13676358 e CMake 3.22.1. Configure `ANDROID_HOME` ou `android/local.properties`.
Não há Gradle wrapper binário neste repositório.

```sh
python3 android/tools/prepare_core_sdk.py
gradle -p android assembleDebug lintDebug
```

O CI verifica assinatura de debug, ABI AArch64 real, alinhamento ELF e ZIP de
16 KiB, manifesto sem permissões e ausência de formatos de arquivos do jogo.
Isso valida compilação/empacotamento, não execução no Odin nem desempenho.

Referências: [NativeActivity](https://developer.android.com/ndk/samples/sample_na),
[superfície Vulkan Android](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateAndroidSurfaceKHR.html),
[AGP 8.9 e ferramentas compatíveis](https://developer.android.com/build/releases/agp-8-9-0-release-notes).

## English

Version 0.0.7 is a **single integrated diagnostic package**. In addition to
all earlier checks, it runs three memory/thread/synchronization rounds with
four actual SDK-created PPU workers each (12 workers). The extra worker is
translated from synthetic PPC by the pinned lifter. It uses SDK mutex/semaphore
handlers, updates a shared counter, signals completion, and exits through the
real SDK thread-exit/join path. A post-exit store must never execute. Admission
is semaphore-serialized; this is not a general SDK race stress test.

Checks include allocation/zero fill/accounting/free, VM commit/protection/page
discard, revoked access after release, mutex cross-thread timeout/deadlock,
semaphore limits/timeouts, invalid handles, and final resource auditing. The
adapter restricts sizes, timeouts and call forms to this private fixture. Incomplete
sys_mmapper acknowledgment-only paths are unavailable. The prepared SDK thread
copy excludes cross-PPU reservation registration and raw-SPU interrupt re-entry;
this fixed worker has no reservation instructions. Both capabilities remain
explicitly unvalidated/unavailable, with no fake handlers. No SPU jobs are
registered. A worker epoch restarts only after all prior tracked workers were
reaped, and a completion watchdog aborts rather than releasing a live worker's VM.
The SDK longjmp path crosses no C++ objects requiring destruction.

Expect `bundle_status=passed`, `bundle_failures=0`, `bundle_rounds=3` and
`bundle_workers_created_and_joined=12`. Full game boot, SPU scheduling, audio
and game rendering remain unavailable; no ISO is needed.

The services test introduced in 0.0.6 extends the fixture to ten lifted PPC functions and connects
**two actual SDK time syscall handlers (145/147) and cellRtcGetCurrentTick**.
The bridge uses the SDK LV2/NID tables; build-time and runtime NIDs must match.
The complete unmodified sys_timer.c/cellRtc.c modules are compiled with individual
sections; the linker retains only selected handlers, excluding timer/event workers.
The bridge validates full 64-bit pointer ranges and alignment before invoking
handlers, copies the ABI-checked context without type aliasing, checks host time,
RTC epoch/units and big-endian outputs. NULL returns real CELL_EINVAL; unavailable
syscalls/NIDs return CELL_ENOSYS and an explicit unsupported-call error.
Expect `services_status=passed`, `services_failures=0`, `dispatch_status=passed`
and `dispatch_generated_functions=10`. This is not full HLE, scheduling or game
boot. No ISO is needed.

The dispatch test introduced in 0.0.5 runs **six genuinely lifted synthetic PPC functions**: the
locked SDK lifter translates our own PPC words at build time, then the NDK
compiles the generated C++ into ARM64. No game code or Python runtime ships.
The fixture's image, function map, lifter inputs and generated files are hashed.
The SDK registry subset is reused verbatim, with a limited test-only call bridge
rather than the full SDK dispatcher/ppu_run. It checks the generated context ABI,
OPD/TOC, arithmetic, big-endian load/store, direct/indirect calls, tail trampolines,
registry collisions, missing targets, memory bounds and thread-local isolation.
An unsupported synthetic `sc` must return an error for the test to pass.
This bridge does not preempt arbitrary lifted loops; it accepts only our fixed
six functions. Core, ELF and dispatch probes own their VMs sequentially.
Expect `dispatch_status=passed`, `dispatch_failures=0`,
`dispatch_generated_functions=6`, `dispatch_lifted_code_executed=true`, and
`dispatch_game_executed=false`. Disgaea boot, full HLE/scheduling and game RSX
are still unavailable. No ISO is needed for this stage.

The ELF test retained from 0.0.4 additionally tests a **pinned SDK ELF validation/segment-copy
subset**, after the core probe releases its VM. It loads only an internally
created synthetic ELF, checks PT_LOAD/BSS, entry OPD/TOC, PT_TLS metadata,
29 rejected malformed/truncated fixtures, successful reload, and file/VM cleanup.
The parser bytes come verbatim from the locked Git object, with a checked
extraction hash; debugger hooks and guest dispatch are excluded. No PPC code
executes. Expect `elf_status=passed`, `elf_failures=0`, and
`elf_rejected_fixtures=29`. This is not the complete PPU loader or game boot.
Do not pass user images to this private test entry. A bounded reader and segment
commit plan are required before user-selected files can be accepted.

The core test retained from 0.0.3 links selected real SDK primitives, not the full game runtime.
A one-shot background probe checks a 4 GiB aligned virtual reservation (about
256 MiB of main memory is touched and then released), endian access, top-page
protection, PPU integer and inline CAS behavior, 20,000 contested guest-memory
increments using four SDK host threads, events/semaphore, and the SDK's own SPU
instruction/numeric fixtures. ARM64 uses the SDK's existing NEON paths.

Wait about 10 seconds before sharing. Expect `core_status=passed` and
`core_failures=0`. Full PPU/SPU scheduling, loader reservation registry/ABA,
SPU-context coherence, syscalls/HLE, game loading and gameplay remain unvalidated.
Optional inactive debug/loader hooks fail closed if reached. This test owns its
VM; it must be removed or process-isolated before a game loader shares that VM.
Sources come from the locked SDK Git tree, without editing the SDK worktree or
reading any generated game code. SDK license notices are included in the APK.
Build-copy Android adaptations replace unavailable pthread cancellation with
cooperative exits for registered workers at poll boundaries, and use Android
thread IDs for affinity. The test observes a real poll waiter, requests stop,
joins it and checks waiter cleanup. Other HLE blocking waits and full scheduler
shutdown remain unvalidated. The probe does not request CPU affinity changes.

**Diagnostic APK; it does not run the game yet.** Initial Android ARM64 platform
test, targeting the AYN Odin 2 Portal first. Reuses the project's experimental
Vulkan renderer with Android window/surface support, NativeActivity lifecycle
handling and physical gamepad event counters. The Windows release is unchanged.

Install the APK, open **D2 Android • Dispatch test**, and check for a colored
triangle and the diagnostic notice. Press physical buttons/D-pad and move the
sticks. Background/restore the app twice, lock/unlock the screen, then use
**Share diagnostic report** and describe any visual issue or crash. No ISO or
storage permission is needed. Requires Android 9+, ARM64 and Vulkan 1.1; actual
device execution is not yet validated. Debug signing is for development only.

The report includes GPU/API, submitted test-frame count, lifecycle and input
event counters, without personal paths or game content. No data is sent
automatically. The left-stick axes are recorded; game controls are not wired up.

Remaining work includes complete RSX-to-Vulkan integration, Android ARM64 PPU/SPU
runtime validation, audio, input, document-descriptor/VFS support, update/DLC and
save storage, a user-owned game preparation strategy and thermal/performance
testing. No game-derived code or assets are bundled.

Build with JDK 17, Gradle 8.11.1, SDK/Build Tools 35, NDK 28.2.13676358 and CMake
3.22.1: `python3 android/tools/prepare_core_sdk.py
gradle -p android assembleDebug lintDebug`. CI checks actual AArch64 ELF,
16 KiB alignment, APK signing and package contents; it does not prove device
compatibility or playable performance.
