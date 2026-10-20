# Disgaea D2 — Android ARM64 bootstrap 0.0.4

## Português

### Teste do subconjunto ELF na 0.0.4

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

1. Baixe o APK `DisgaeaD2-Android-0.0.4-ELF-Test-arm64.apk` e instale no Odin.
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

Version 0.0.4 additionally tests a **pinned SDK ELF validation/segment-copy
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

Install the APK, open **D2 Android • ELF test**, and check for a colored
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
