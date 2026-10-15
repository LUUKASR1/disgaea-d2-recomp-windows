# Verificador RSX → HLSL → SPIR-V → GPU Vulkan

Teste experimental de execução de shaders sintéticos. **Não abre o Disgaea nem altera o launcher, os saves ou a instalação do jogo.**

1. Extraia o ZIP em uma pasta gravável.
2. Abra `DisgaeaD2RsxGpuProbe.exe`. Ele executa sem janela gráfica e fecha o terminal ao terminar.
3. Envie a pasta `rsx-gpu-check`, criada ao lado do executável quando aberto pelo Explorer. Ela contém `rsx-gpu-report.json` e `adapter.txt`; em caso de falha, também `rsx-gpu-error.txt`.

O resultado esperado é `status: passed`, `cases_passed: 16` e `pixels_verified: 49152`. São 16 imagens de 64×48, verificadas canal por canal com tolerância máxima de 1/255. Os testes cobrem atributos RSX, orientação vertical, escala/translação por constantes, unidade de textura 3 independente da unidade 0, textura branca para unidades não vinculadas, blend, descarte por alfa e substituição de recursos após submissões.

Precisa apenas de um driver com Vulkan 1.1. Não requer SDK, compilador instalado, extração ou arquivos do jogo. O DXC oficial está embutido e é carregado por caminho absoluto numa pasta temporária exclusiva, removida no encerramento normal. As licenças acompanham o pacote.

`--output pasta` muda a pasta do relatório. `--validation` é uma opção para desenvolvedores com a camada Khronos instalada; **não é necessária para testar no seu PC**. O relatório diferencia se essa camada estava habilitada.

`unavailable` (saída 77) significa ausência de loader/dispositivo compatível. `failed` (saída 1) indica erro. Os relatórios dizem explicitamente `real_game_shaders_tested: false` e `game_backend_ready: false`: passar aqui ainda não comprova os shaders reais nem um backend Vulkan jogável.

## English

Experimental synthetic RSX shader GPU execution checker, not a playable game backend. Extract and run `DisgaeaD2RsxGpuProbe.exe`. No graphics window appears. Send the generated `rsx-gpu-check` folder. Expected: 16 cases and 49,152 verified pixels. Requires a Vulkan 1.1 graphics driver; DXC is embedded and no SDK or game files are needed. `--validation` requires developer-installed Khronos layers and is optional. Real game shaders have not been tested. The existing game launcher remains on Direct3D 12.
