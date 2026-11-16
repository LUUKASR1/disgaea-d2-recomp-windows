# Porte Android — plano (AYN Odin 2 Portal e AYN Thor)

## Português

### Ponto de partida

- Existe a branch `feature/android-bootstrap` (Android 0.0.15): app Java + código nativo ARM64, leitura de ISO por
  documento (SAF), verificação de SELF/ELF e serviços de arquivo LV2. **O jogo ainda não inicia.**
- O renderizador Vulkan do projeto (`port/vulkan`, branch `vulkan-backend`) é reaproveitado pelo CI do Android.
- O Windows 1.0.x usa DirectX 12; o Android usa Vulkan (o Snapdragon 8 Gen 2 tem driver Vulkan 1.3).

### Aparelhos

| | AYN Odin 2 Portal | AYN Thor |
|---|---|---|
| SoC | Snapdragon 8 Gen 2 | Snapdragon 8 Gen 2 (modelos Base/Pro/Max) |
| Telas | 1 (7", 1080p) | 2: 6" 1920×1080 120 Hz (cima) + 3,92" 1240×1080 60 Hz (baixo) |
| Android | 13 | 13 |

O Thor Lite usa Snapdragon 865 e fica fora da meta inicial.

### Decisão importante: de onde vem o código do jogo no Android

No Windows o launcher gera o executável localmente a partir dos arquivos do usuário, e nada do jogo é distribuído.
No Android não dá para compilar C no aparelho. Opções:

1. **Preparar no PC (recomendada para o primeiro marco):** uma ferramenta do launcher do Windows gera a biblioteca
   ARM64 (`.so`) a partir da ISO do usuário; o usuário copia a pasta para o aparelho. Nada do jogo é distribuído.
2. Interpretador/JIT de PowerPC no aparelho: não depende do PC, porém muito mais trabalho e risco de desempenho.
3. Gerar e distribuir a biblioteca pronta: **descartada** (seria distribuir código derivado do jogo).

### Fases

1. **Executar o núcleo no ARM64:** compilar o código recompilado para ARM64 (NDK), memória guest, serviços LV2,
   arquivos da ISO via SAF. Meta: o jogo chega à tela de título no aparelho (log de diagnóstico).
2. **Gráficos:** conectar o backend Vulkan a uma `Surface` Android (swapchain, rotação, 120 Hz no Thor), com os
   filtros do Windows (FSR, nitidez, contorno) só depois de estável.
3. **Áudio e entrada:** áudio nativo (AAudio/Oboe), controle embutido (botões, analógicos, gatilhos) mapeado para o
   mapa já usado no Windows; menu de opções do jogo adaptado para toque e controle.
4. **Saves:** mesmo formato do Windows (RPCS3), com importar/exportar por SAF.
5. **Thor com duas telas:** o jogo na tela de cima; na de baixo (via `DisplayManager`/`Presentation`) painel com
   mapa de comandos, opções rápidas (FSR, FPS, captura), estado de saves e, se o jogador preferir, a mesma imagem do
   jogo espelhada ou ampliada. Cada tela com taxa de atualização própria.
6. **Interface:** launcher Android no mesmo estilo cartoon/vidro do Windows (logo, carrossel de gameplay).
7. **Desempenho:** perfis por aparelho (Odin 2 Portal / Thor), controle de temperatura e bateria.

### Riscos

- O desempenho do código recompilado e do Vulkan em Adreno 740 precisa ser medido cedo (fase 1–2).
- Cada pacote (BLUS31313 1.40, e uma eventual BLES01939) precisa do próprio executável recompilado.
- Mudanças de ABI/permissões do Android (Scoped Storage) afetam leitura de ISO e saves.

### Próximo passo imediato

Retomar a fase 1 a partir da 0.0.15: tirar do bootstrap os diagnósticos de SELF e iniciar a execução do núcleo
recompilado, com um relatório único por teste no aparelho.

## English

### Starting point

`feature/android-bootstrap` (Android 0.0.15) already has an ARM64 app, document-based ISO reading, SELF/ELF checks
and LV2 file services; **the game does not start yet**. The project's Vulkan renderer is reused. Windows 1.0.x uses
DirectX 12; Android uses Vulkan.

### Targets

AYN Odin 2 Portal (one 7" 1080p screen) and AYN Thor (6" 1920×1080 120 Hz top + 3.92" 1240×1080 60 Hz bottom).
Both: Snapdragon 8 Gen 2, Android 13. Thor Lite (Snapdragon 865) is out of the first scope.

### Key decision: where the game code comes from

On Windows the launcher generates the executable locally; no game content is distributed. Android cannot compile C on
the device, so: (1) **prepare on the PC** — the Windows launcher generates the ARM64 library from the user's own ISO
and the user copies it over (recommended for the first milestone); (2) a PowerPC interpreter/JIT on the device (much
more work, performance risk); (3) shipping a prebuilt library is **ruled out** (derived game code).

### Phases

1. Run the core on ARM64 (NDK build, guest memory, LV2 services, ISO files through SAF) up to the title screen.
2. Vulkan on an Android `Surface` (swapchain, rotation, 120 Hz); Windows filters later.
3. Audio (AAudio/Oboe) and built-in gamepad input using the Windows mapping; touch/controller-friendly options menu.
4. Saves in the same RPCS3 layout with import/export.
5. Thor dual screen: game on the top display; command map, quick options (FSR/FPS/capture) and save status on the
   bottom display (`DisplayManager`/`Presentation`), with an optional mirrored/zoomed game view.
6. Android launcher in the same cartoon/glass style (logo, gameplay carousel).
7. Per-device performance profiles, thermals and battery.

### Risks

Performance of recompiled code and Vulkan on Adreno 740 must be measured early; each game build (BLUS31313 1.40 and a
possible BLES01939) needs its own recompiled executable; Android Scoped Storage affects ISO and save access.

### Immediate next step

Resume phase 1 from 0.0.15: drop the SELF diagnostics from the bootstrap and start executing the recompiled core, with a
single report per on-device test.
