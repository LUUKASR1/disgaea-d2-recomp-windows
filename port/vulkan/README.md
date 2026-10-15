# Disgaea D2 — base Vulkan experimental

Este executável testa o novo backend Vulkan. **Ainda não executa o jogo.** O launcher do jogo continua usando Direct3D 12.

Etapa atual: envio de texturas RGBA8, UVs, sampler/descriptor, transparência e descarte por alpha, além dos buffers reais de vértices e índices. O relatório schema 3 inclui `renderer_stage: rgba-textures` e informa se a validação estava habilitada.

1. Extraia o ZIP em uma pasta gravável.
2. Abra `DisgaeaD2VulkanProbe.exe`.
3. Devem aparecer quatro áreas: vermelho no alto à esquerda, verde no alto à direita, azul embaixo à esquerda e roxo embaixo à direita. O azul é o fundo através de um texel transparente; o roxo é vermelho semitransparente sobre o azul.
4. Redimensione, minimize/restaure e feche com Alt+F4. **Agora a janela permanece aberta até você fechá-la**, sem precisar de comando no terminal.
5. Envie `DisgaeaD2-Vulkan-report.json`, criado na pasta de execução. Ele registra mensagens de tamanho/minimização/restauração/fechamento, sem afirmar que identifica a tecla Alt+F4.

Não requer Vulkan SDK nem compilador instalado. Requer um driver gráfico com Vulkan 1.1 ou superior; o loader Vulkan vem do driver. Os shaders de teste estão embutidos no executável. Não contém arquivos do jogo.

Opções: `--no-vsync`, `--frames 10000`, `--self-test` (renderização e leitura de pixels sem janela), `--report caminho.json`. `--validation` é somente para desenvolvedores com a camada Khronos instalada.

`status: passed` comprova somente o teste indicado, não compatibilidade do jogo. `status: unavailable` significa ausência de loader/dispositivo compatível (código de saída 77); `failed` significa erro (1).

## English

Experimental native Vulkan foundation; **not a playable game backend**. Extract the ZIP to a writable directory and run `DisgaeaD2VulkanProbe.exe`. Four quadrants should appear: red/green above, blue/purple below. The lower quadrants exercise transparent and semitransparent texels. The window now remains open until closed. Test resize, minimize/restore and Alt+F4. Send `DisgaeaD2-Vulkan-report.json` and describe window behavior.

No Vulkan SDK or compiler is required. A graphics driver supporting Vulkan 1.1 is required. The game launcher still uses Direct3D 12. Embedded shaders are project-owned; no game assets are included.
