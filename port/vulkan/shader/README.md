# Verificador de shaders RSX → HLSL → SPIR-V

Este executável é um teste de compilação. **Não abre o jogo nem uma janela Vulkan.** Não requer Vulkan SDK, compilador instalado ou arquivos do jogo.

1. Extraia o ZIP em uma pasta gravável.
2. Abra `DisgaeaD2ShaderProbe.exe` e aguarde o fechamento da janela do terminal.
3. Envie `shader-check/shader-report.json`. Se o resultado for failed, envie também `shader-check/shader-error.txt`.

O DXC oficial v1.8.2505.1 está embutido no executável Windows. Ele é extraído temporariamente em uma pasta exclusiva, carregado pelo caminho completo e removido ao encerrar normalmente. Um encerramento forçado pode deixar essa pasta temporária. As licenças acompanham o pacote.

O teste monta pequenos programas RSX sintéticos, passa pelos decompiladores auditados do projeto e compila o HLSL em memória para SPIR-V. Verifica atributos, varyings, constantes, texturas/samplers e rejeição de código inválido. Usa HLSL 2018 para preservar as condições vetoriais da saída existente.

O relatório deixa explícito `real_game_shaders_tested: false` e `game_backend_ready: false`. Compilar esses programas não demonstra equivalência dos shaders reais nem renderização do Disgaea em Vulkan. O jogo continua usando DirectX 12.

## English

Synthetic RSX shader compilation checker, not a game or Vulkan rendering test. Extract and run `DisgaeaD2ShaderProbe.exe`; send `shader-check/shader-report.json`, plus `shader-error.txt` if it fails. DXC is embedded and extracted temporarily; no SDK/compiler installation or game files are needed. The included licenses apply. Real game shaders and a playable Vulkan backend are not validated by this test.
