# Disgaea D2 — base Vulkan experimental

Este executável testa o novo backend Vulkan. **Ainda não executa o jogo.** O launcher do jogo continua usando Direct3D 12.

1. Extraia o ZIP em uma pasta gravável.
2. Abra `DisgaeaD2VulkanProbe.exe`.
3. Deve aparecer um triângulo colorido sobre fundo escuro. A janela fecha automaticamente após 300 quadros. Para testar redimensionamento e Alt+F4 com mais tempo, execute `DisgaeaD2VulkanProbe.exe --frames 10000` pelo terminal.
4. Envie `DisgaeaD2-Vulkan-report.json`, criado na pasta de execução. Informe se redimensionar, minimizar/restaurar e fechar funcionaram.

Não requer Vulkan SDK nem compilador instalado. Requer um driver gráfico com Vulkan 1.1 ou superior; o loader Vulkan vem do driver. Os shaders de teste estão embutidos no executável. Não contém arquivos do jogo.

Opções: `--no-vsync`, `--frames 10000`, `--self-test` (renderização e leitura de pixels sem janela), `--report caminho.json`. `--validation` é somente para desenvolvedores com a camada Khronos instalada.

`status: passed` comprova somente o teste indicado, não compatibilidade do jogo. `status: unavailable` significa ausência de loader/dispositivo compatível (código de saída 77); `failed` significa erro (1).

## English

Experimental native Vulkan foundation; **not a playable game backend**. Extract the ZIP to a writable directory and run `DisgaeaD2VulkanProbe.exe`. A colored triangle should appear and the window closes after 300 frames. Use `--frames 10000` for resize, minimize/restore and Alt+F4 testing. Send the generated `DisgaeaD2-Vulkan-report.json` and describe window behavior.

No Vulkan SDK or compiler is required. A graphics driver supporting Vulkan 1.1 is required. The game launcher still uses Direct3D 12. Embedded shaders are project-owned; no game assets are included.
