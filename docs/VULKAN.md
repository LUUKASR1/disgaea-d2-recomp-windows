# Backend Vulkan — implementação por etapas

O runtime auditado possui Direct3D 12 e Metal. A opção Vulkan precisa de um backend novo; adicionar uma seleção no launcher sem esse backend não altera a API usada pelo jogo.

## Etapa implementada: fundação independente

`port/vulkan` contém um executável C++20 com carregamento dinâmico do loader do driver, seleção de adaptador, fila gráfica, memória, comandos, pipeline SPIR-V, renderização e leitura de pixels. No Windows inclui superfície Win32, swapchain, V-Sync, recriação ao redimensionar, minimização e fechamento. Os semáforos de apresentação são separados por imagem do swapchain. O teste é isolado do runtime do jogo.

Shaders GLSL próprios são compilados durante o desenvolvimento e incorporados em `triangle_spv.h`. O executável usa runtime MSVC estático e não exige SDK ou ferramentas no computador do usuário. Os headers Khronos são fixados no commit `29f979ee5aa58b7b005f805ea8df7a855c39ff37` (Vulkan-Headers v1.3.296).

O relatório informa explicitamente `game_backend_ready: false`. `passed` valida apenas a inicialização/renderização do probe. A validação Linux usa Mesa lavapipe, não mede desempenho de uma GPU física. O runner Windows pode não ter um ICD Vulkan: nesse caso registra `unavailable`, sem tratar isso como renderização bem-sucedida.

## Integração ainda necessária

1. Extrair uma interface de backend dos comandos RSX atualmente atendidos pelo adaptador D3D12.
2. Implementar buffers, texturas, render targets, profundidade/stencil, sincronização, estados e tradução dos shaders do jogo para SPIR-V.
3. Validar cenários, sprites, alpha/blend, efeitos, HUD e apresentação usando Vulkan; comparar com D3D12.
4. Conectar resolução, tela cheia e V-Sync às configurações do launcher e oferecer seleção da API somente quando o jogo funcionar nesse backend.

FSR, DLSS e geração de quadros precisam de integração e validação próprias nesse caminho. A fundação não promete ganho de FPS nem conclui essas funcionalidades.

## Compilação e testes

```
cmake -S port/vulkan -B build-vulkan
cmake --build build-vulkan --config Release
ctest --test-dir build-vulkan -C Release --output-on-failure
```

Para usar headers locais, passe `-DD2_VULKAN_HEADERS_DIR=caminho`. Para recompilar os shaders, instale `glslangValidator` e `spirv-val` e execute `python port/vulkan/generate_shaders.py`. O teste `--self-test --validation` exige `VK_LAYER_KHRONOS_validation` instalada. O fluxo `.github/workflows/vulkan.yml` gera o probe Windows separado da build jogável.

Limites desta primeira implementação: uma fila que suporte gráficos e apresentação, uma submissão gráfica por vez, nenhuma tradução RSX, nenhum asset do jogo. O renderer foi mantido independente para que falhas experimentais não alterem o jogo funcional.
