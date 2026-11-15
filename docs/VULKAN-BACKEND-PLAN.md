# Plano: backend Vulkan jogável (rsx_vulkan_backend)

Estado: M1 concluído e verificado no jogo (RTX 5070, 1.140 frames, encerramento limpo); M2 em andamento (coletor de estatísticas de draws, ainda sem tradução); M3+ pendentes. Base analisada: SDK ps3recomp `a679051`
(+ `patches/ps3recomp-d2-windows.diff`) e `port/vulkan` (d2_vulkan, etapas 1-8 de `VULKAN.md`).

## O que a análise confirmou

1. **A interface de backend já existe.** `rsx_commands.h` define `rsx_backend` (callbacks `init`,
   `begin_frame`, `end_frame`, `present`, `clear`, `set_*`, `draw_arrays`, `draw_indexed`,
   `bind_texture`) e `rsx_set_backend()`. O backend D3D12 (`rsx_d3d12_backend.c`, ~7.400 linhas)
   e o Metal apenas preenchem essa tabela. O Vulkan segue o mesmo modelo; não é preciso
   refatorar o SDK, só adicionar um terceiro backend.
2. **A escolha D3D12/Metal em `port/main.cpp` é em tempo de compilação** (`#define rsx_backend_init ...`).
   Para Vulkan precisa virar uma tabela de ponteiros escolhida por `D2_GRAPHICS_API`
   (`backend_choice.h` já lê a variável).
3. **O backend D3D12 grava um `D3D12DrawRecord` por draw** (shader FP/VP, constantes, 4 unidades de
   textura, blend/cull/color-mask, depth/stencil key, alvo/MRT, viewport) e reproduz no fim do frame.
   `d2_vulkan` já tem os equivalentes: `acquireRsxPipeline` (shader+estado), `createRsxConstants`,
   `DrawCommand`, `setTextureUnit(Blocks)`, `offscreen(..., color_targets)`.
4. **As adaptações do projeto (`adapt_graphics.py`) são presas ao hash do arquivo D3D12.** O backend
   Vulkan deve ser um arquivo novo e aditivo; o caminho D3D12 permanece intacto como padrão.

## Mapeamento callback -> d2_vulkan

| Callback / dado RSX | Implementação Vulkan |
| --- | --- |
| `init` | janela Win32 (reaproveitar a do D3D12/N-window), `Renderer::start(hwnd)`, `resize` |
| `begin_frame` / `end_frame` | zerar lista de draws; `draw(span<DrawCommand>)` no fim |
| `clear` (display) | `DrawParameters::clear_color/clear_depth` |
| `clear` (offscreen) | draw de quad ou clear dedicado (**falta** em d2_vulkan) |
| `set_blend` / `set_color_mask` / cull | `RasterState` (já cobre fatores, op, máscara, cull, ccw) |
| `set_depth_stencil` | `DepthMode` + `depth_compare`; **stencil não existe** |
| `set_shader` | VP/FP -> HLSL (decompiladores do SDK) -> `compiler.cpp` (DXC) -> SPIR-V -> `acquireRsxPipeline`, cache por hash do ucode |
| constantes VP/FP | `createRsxConstants` por draw (limite 64 estados: **reciclar por frame**) |
| `bind_texture` | `setTextureUnit` / `setTextureUnitBlocks` (BC1/2/3); hoje texturas são globais ao renderer |
| `set_vertex_attribs`, `draw_*` | `setRsxGeometry` (16 atributos float4) + `DrawCommand` |
| `present` | `drawAppend` + swapchain; `needsResize`/`resize` com VSync do launcher |

## Lacunas conhecidas (ordem sugerida)

1. ~~Texturas por draw e constantes por frame~~ — feito no M2 (blocos deduplicados com cópia sombra na CPU; leitura da memória mapeada da GPU custava ~38 ms/frame).
2. **Render-to-texture e alvos intermediários persistentes** (`rt_off`, `rt_mrt`, `tex_rt` do D3D12);
   formatos heterogêneos e vínculo dinâmico de alvos.
3. ~~Depth + stencil~~ — feito: `D32_SFLOAT_S8_UINT` (fallbacks D24S8/D32/D16), estado de stencil por pipeline (ex.: recorte circular do retrato "ATTACK ENTRY"). Pendente: clear de stencil no meio do frame e referência distinta por face.
4. Vertex textures, cubemaps, amostragem do frame anterior.
5. Desempenho: submissão sem fence por draw, compilação de pipeline fora da thread de render.
6. FSR/DLSS/frame generation (só depois de 1-5 estáveis).

## Marcos de validação (cada um exige teste na GPU do usuário)

- M1: janela Vulkan, `clear` + present sem crash, flips do jogo retirados corretamente.
- M2: menu inicial/HUD (draws de sprites, alpha) comparando com D3D12 via `DisgaeaD2DrawReplay`. **Status: jogo abre, menu e jogabilidade renderizam sem glitches (testado na GPU do usuário); ~9 ms/frame de gravação em cena pesada.**
- M3: batalha com cenário e personagens (BC2/BC3, múltiplos pipelines).
- M4: efeitos (partículas, Mothman), save/load, DLC.
- M5: seletor do launcher liberado, com fallback explícito (nunca silencioso). **Status: implementado (aguarda teste do usuário):** o launcher habilita "Vulkan (experimental)" e define `D2_VULKAN_EXPERIMENTAL=1`, `D2_VULKAN_FALLBACK=d3d12` e o cache de shaders; se o build não tiver Vulkan ou a inicialização falhar, o jogo mostra um aviso e usa DirectX 12. `D2_VULKAN_FORCE_INIT_FAIL=1` força a falha para testar o fallback.

## Riscos

- Escopo equivalente a reescrever o backend D3D12 de ~7.400 linhas.
- Sem GPU/Windows neste ambiente: compilação MSVC/clang-cl e execução só no PC do usuário.
- Primeira execução do Vulkan não deve afetar saves nem o perfil D3D12.

## Como os usuários recebem o Vulkan (build e launcher)

- `tools/build_windows.py` lê `D2_VULKAN_GAME_BACKEND`: ausente/`0` = só DirectX 12 (padrão, build estável); `1` = Vulkan obrigatório; `auto` = tenta incluir o Vulkan e, se não conseguir configurar ou compilar (por exemplo sem rede para baixar o DXC fixado), refaz o build só com DirectX 12 para o jogo continuar compilando.
- O launcher de preview (não oficial) define `D2_VULKAN_GAME_BACKEND=auto` ao compilar; a versão oficial estável continua só DirectX 12.
- No launcher, "Vulkan (experimental)" liga `D2_GRAPHICS_API=vulkan` com fallback explícito para DirectX 12. Isso vale a partir de um launcher reempacotado (`tools/package_launcher.py`).
- Sem launcher reempacotado: `Compilar-Jogo-Vulkan.bat` e `Rodar-Jogo-Vulkan.bat`.
- Limitações e testes pendentes: save/load, DLC, sessões longas, outras GPUs, render-to-texture e cubemaps.

