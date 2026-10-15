# Comparação de um desenho isolado: DirectX 12 × Vulkan

No launcher experimental 0.1.49, habilite captura, escolha **Desenho real — comparar Vulkan**, abra o jogo e pressione **F8** numa cena parada. Comece pela tela inicial ou por um menu. O coletor tenta até oito desenhos compatíveis e guarda até quatro por solicitação, com orçamento de 256 MiB de payload por sessão. Ao fechar o jogo, gera o ZIP em Downloads. Os dados capturados ficam locais e não são distribuídos com o projeto.

Cada `frame_XX/draw_NNN` completo contém fontes finais VS/FP associadas por hash do bytecode, vértices com 16 atributos, constantes VP/FP do slot/paridade submetidos, quatro texturas efetivamente vinculadas (já convertidas com o remap do SRV) e uma imagem RGBA de referência. A referência usa o PSO original, buffers e descritores originais em alvo próprio, limpo com RGBA zero e depth 1. **Não é a imagem final da cena nem prova de visibilidade na cena.**

Para desenvolvedores, extraia o ZIP e execute no PowerShell:

```powershell
.\DisgaeaD2DrawReplay.exe --input "C:\captura\frame_00\draw_005" --output "draw-vulkan-check"
```

O EXE contém DXC; não precisa de SDK ou compilador instalado. Requer um driver Vulkan 1.1; depth clamp, quando solicitado pelo estado capturado, requer suporte do dispositivo. `--validation` é opcional e exige camada Khronos instalada.

O resultado inclui `draw-vulkan-report.json`, `output.bmp`, `reference.bmp` e `difference.bmp`. `exact-match` significa todos os canais idênticos; `within-tolerance` admite diferença de até 1/255 por canal. `pixel-mismatch` indica diferença maior. Referência toda zero gera `empty-reference`, nunca aprovação. Falhas e entradas incompletas não reutilizam imagens de uma execução anterior. `error.txt` informa recursos não suportados/erros.

Escopo inicial: um draw expandido de triângulos, até 4096 vértices, viewport completo até 1920×1080, um alvo RGBA8 realmente vinculado. Saídas FP adicionais são registradas por reflexão e descartadas somente quando não têm alvo vinculado, como no pipeline D3D12 capturado. Quatro SRVs 2D imutáveis de um mip: RGBA8, R8, R8G8 ou BC1/BC2/BC3 UNORM, até 4096×4096 / 4 milhões de texels por unidade. Samplers correspondem ao root signature auditado: s0 point/clamp, s1–s3 linear/wrap, com a opção palette-clamp em s1. Render targets como entrada, cubemaps, stencil, bias e formatos adicionais são recusados. Não há reconstrução de entradas de outros passes, comandos RSX completos ou Vulkan jogável. O alvo Vulkan usa depth float em vez do D24 original; diferenças devem ser investigadas, não ocultadas por tolerância ampla.

O `--self-test` utiliza fontes e dados próprios com uma referência calculada, não um shader do jogo. Há ainda um teste WARP que exporta a referência D3D12 de um PSO próprio. A comparação dos desenhos reais depende da coleta do usuário. O launcher continua usando DirectX 12.

## English

Experimental single-draw D3D12/Vulkan comparison, not a playable backend. Launcher 0.1.49 adds an explicit F8 **Desenho real — comparar Vulkan** capture. Run `DisgaeaD2DrawReplay.exe --input path\to\draw_NNN --output draw-vulkan-check`. DXC is embedded. Reports distinguish exact match, 1/255 channel tolerance, mismatch, empty reference and unavailable driver. Only bounded immutable 2D inputs, a full viewport, one RGBA8 output and a single FP target are accepted. Game files are never included in distributed packages. Synthetic self-tests do not validate game rendering.

Pacotes v3 preservam blocos BC em `texN.bc` e o remapeamento de componentes; a GPU Vulkan decodifica o mesmo payload. Requer `textureCompressionBC` no adaptador. Pacotes v1/v2 RGBA continuam aceitos. Recusas incluem propriedades do recurso/view e a unidade que bloqueou a coleta.
