# Disgaea D2 — base Vulkan experimental

Este executável testa o novo backend Vulkan. **Ainda não executa o jogo.** O launcher do jogo continua usando Direct3D 12.

Etapa atual: depth buffer e lista de desenhos por quadro, além das texturas RGBA8 e transparência. O relatório schema 4 inclui `renderer_stage: depth-draw-lists` e informa se a validação estava habilitada.

1. Extraia o ZIP em uma pasta gravável.
2. Abra `DisgaeaD2VulkanProbe.exe`.
3. Deve aparecer um fundo vermelho, um retângulo verde no centro e um retângulo azul no canto superior esquerdo. A ordem do fundo e do objeto central alterna a cada 120 quadros; **a imagem deve permanecer igual, sem piscar**. Uma camada invisível também testa descarte antes de escrever profundidade.
4. Redimensione, minimize/restaure e feche com Alt+F4. **Agora a janela permanece aberta até você fechá-la**, sem precisar de comando no terminal.
5. Envie `DisgaeaD2-Vulkan-report.json`, criado na pasta de execução. Ele registra mensagens de tamanho/minimização/restauração/fechamento, sem afirmar que identifica a tecla Alt+F4.

Antes da janela, um teste offscreen verifica os 12.288 pixels da cena em quatro quadros com ordens alternadas. O relatório registra `preflight_pixels_verified` e `preflight_adapter` separadamente do adaptador/janela de apresentação. Isso valida a cena de teste, não os gráficos do jogo.

Não requer Vulkan SDK nem compilador instalado. Requer um driver gráfico com Vulkan 1.1 ou superior; o loader Vulkan vem do driver. Os shaders de teste estão embutidos no executável. Não contém arquivos do jogo.

Opções: `--no-vsync`, `--frames 10000`, `--self-test` (renderização e leitura de pixels sem janela), `--report caminho.json`. `--validation` é somente para desenvolvedores com a camada Khronos instalada.

`status: passed` comprova somente o teste indicado, não compatibilidade do jogo. `status: unavailable` significa ausência de loader/dispositivo compatível (código de saída 77); `failed` significa erro (1).

## English

Experimental native Vulkan foundation; **not a playable game backend**. Extract the ZIP to a writable directory and run `DisgaeaD2VulkanProbe.exe`. Expect a red background, green center rectangle and blue top-left overlay. Submission order changes every 120 frames; the image must remain identical. An automatic offscreen readback checks all 12,288 pixels before showing the window; its adapter is reported separately. The window remains open until closed. Test resize, minimize/restore and Alt+F4. Send `DisgaeaD2-Vulkan-report.json` and describe window behavior.

No Vulkan SDK or compiler is required. A graphics driver supporting Vulkan 1.1 is required. The game launcher still uses Direct3D 12. Embedded shaders are project-owned; no game assets are included.
