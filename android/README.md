# Disgaea D2 — Android ARM64 bootstrap 0.0.1

## Português

**APK de diagnóstico; ainda não executa o jogo.** Primeiro passo do port para
Android, com foco inicial no AYN Odin 2 Portal. Não altera a versão Windows.

O app reaproveita o renderer Vulkan experimental do projeto, agora com
`ANativeWindow`, `VK_KHR_android_surface` e o loader Android `libvulkan.so`.
Usa NativeActivity e a thread de eventos do NDK; libera a superfície antes de
o Android destruir a janela e pausa a renderização ao perder foco ou minimizar.

### Teste no aparelho

1. Baixe o APK `DisgaeaD2-Android-0.0.1-Vulkan-Test-arm64.apk` e instale no Odin.
2. Abra **D2 Android • Teste Vulkan**. Deve aparecer um triângulo colorido e um
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
gradle -p android assembleDebug lintDebug
```

O CI verifica assinatura de debug, ABI AArch64 real, alinhamento ELF e ZIP de
16 KiB, manifesto sem permissões e ausência de formatos de arquivos do jogo.
Isso valida compilação/empacotamento, não execução no Odin nem desempenho.

Referências: [NativeActivity](https://developer.android.com/ndk/samples/sample_na),
[superfície Vulkan Android](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateAndroidSurfaceKHR.html),
[AGP 8.9 e ferramentas compatíveis](https://developer.android.com/build/releases/agp-8-9-0-release-notes).

## English

**Diagnostic APK; it does not run the game yet.** Initial Android ARM64 platform
test, targeting the AYN Odin 2 Portal first. Reuses the project's experimental
Vulkan renderer with Android window/surface support, NativeActivity lifecycle
handling and physical gamepad event counters. The Windows release is unchanged.

Install the APK, open **D2 Android • Vulkan test**, and check for a colored
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
3.22.1: `gradle -p android assembleDebug lintDebug`. CI checks actual AArch64 ELF,
16 KiB alignment, APK signing and package contents; it does not prove device
compatibility or playable performance.
