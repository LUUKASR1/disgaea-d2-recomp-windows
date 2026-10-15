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

## Etapa 2: renderer reutilizável e seleção de API

O teste fornecido pelo usuário passou na NVIDIA GeForce RTX 5070: 300 quadros apresentados em janela, FIFO/V-Sync. A camada de validação estava desativada nesse teste; redimensionamento e fechamento não foram confirmados pelo relatório.

`d2_vulkan` agora é uma biblioteca separada, com interface pública em `vulkan_renderer.h`. Recebe vértices em clip space e índices de 32 bits via buffers Vulkan reais. Expõe desenho indexado/não indexado, clear, viewport, scissor e leitura RGBA8 para testes offscreen. O envio de geometria espera a conclusão da submissão anterior antes de substituir a memória. Dados inválidos são rejeitados antes de gravar buffers; o teste verifica recuperação e preservação da geometria anterior.

O teste GPU compara todos os pixels de um quad indexado, um quad não indexado, recortes de viewport/scissor e um frame somente com clear. Essa interface ainda não interpreta atributos, estados, texturas ou shaders RSX. Suporta apenas uma lista de triângulos por quadro com o shader de cor de teste; não deve ser usada como um renderer completo do jogo.

O launcher 0.1.45 salva `graphics_api` (`d3d12`/`vulkan`), migra perfis antigos para `d3d12` e sobrescreve `D2_GRAPHICS_API` herdado. A interface mostra DirectX 12 disponível e Vulkan em implementação, desabilitado. Uma configuração Vulkan editada manualmente bloqueia a abertura com mensagem explícita. O entry point Windows também rejeita Vulkan/valores desconhecidos antes do boot, para impedir que uma escolha não suportada seja silenciosamente ignorada. Somente D3D12 é um backend jogável nesta etapa.

## Etapa 3: texturas RGBA e transparência básica

O usuário executou a etapa 2 na RTX 5070 por 2.414 quadros: status passed, sem erro registrado, validação desabilitada. O relatório antigo não comprovava os eventos de janela.

O renderer agora envia imagens RGBA8 lineares para imagens Vulkan com tiling optimal, usando staging buffer e barreiras UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY. Um descriptor combinado e um sampler nearest/clamp-to-edge alimentam o shader com UVs por vértice. Uma textura branca de 1×1 preserva o caminho de cor sem textura fornecida. Substituição de textura espera o fence anterior; uploads que excedem o prazo retêm os recursos até o encerramento aguardar device idle.

O shader próprio de teste multiplica textura e cor, descarta alpha abaixo de um limiar configurado e usa blend de alpha direto (não pré-multiplicado). Isso não traduz os comparadores, remaps, formatos compactados, swizzle, mipmaps, estados ou programas RSX do jogo. O limite atual é uma textura RGBA8 até 4096×4096, um mip e uma lista de triângulos por quadro; ainda sem depth/stencil.

Os testes GPU verificam todos os pixels de uma textura assimétrica, UV espelhado, alpha zero/parcial, descarte, largura ímpar e substituições repetidas. Pedidos inválidos não substituem a textura anterior. `--validation` habilita também a validação de sincronização Khronos. O probe schema 3 mostra quatro quadrantes e mantém a janela aberta por padrão. Registra mensagens WM_SIZE, minimização/restauração e WM_CLOSE; esses eventos não identificam qual tecla/botão originou o fechamento.

O jogo continua em D3D12. Esta etapa usa apenas shaders e texturas de teste próprios, sem assets do jogo. A seleção Vulkan para jogar permanece desabilitada.

## Etapa 4: profundidade e lista de desenhos

O teste RGBA do usuário passou na RTX 5070 por 1.384 quadros e registrou eventos de tamanho, uma minimização, uma restauração e WM_CLOSE. Validação estava desabilitada; não houve confirmação visual das cores no relato.

Cada imagem de apresentação agora tem um depth target próprio (D32 float, com D16 como alternativa consultada no dispositivo). O render pass limpa profundidade e cor e oferece seis pipelines: profundidade desligada/somente leitura/leitura e escrita, cada uma com blend ligado/desligado. A comparação quando habilitada é LESS_OR_EQUAL. Não há stencil nesta etapa.

`draw(span<DrawCommand>)` aceita até 4.096 desenhos sequenciais no mesmo pass, usando faixas indexadas ou não indexadas dos buffers enviados. Viewport, scissor, alpha cutoff, blend e profundidade são definidos por desenho. Toda a lista é validada antes de adquirir a imagem ou submeter comandos. Os desenhos ainda compartilham uma textura/shader próprios; isso não traduz comandos RSX nem cria um backend jogável.

Os testes offscreen conferem todos os pixels ao inverter a ordem de objetos opacos, alternar escrita de profundidade, sobrepor uma interface sem depth test, mudar clear depth e descartar fragmentos transparentes antes que escrevam profundidade. Também verificam que uma lista inválida não é parcialmente submetida e que a cena seguinte se recupera.

O probe schema 4 apresenta fundo vermelho, objeto verde no centro e overlay azul no canto superior esquerdo. A ordem dos dois primeiros desenhos alterna a cada 120 quadros, sem mudar a imagem. Antes da janela Win32, outro renderer offscreen confere quatro quadros de 128×96 pixels e registra o adaptador usado nesse preflight separadamente. O jogo continua em Direct3D 12; shaders, texturas/formatos e estados RSX ainda exigem integração.

## Etapa 5: compilação RSX → HLSL → SPIR-V

O relatório do usuário confirmou o preflight de 12.288 pixels na RTX 5070, seguido de 2.105 quadros apresentados e WM_CLOSE sem erro registrado, validação desabilitada.

`shader/compiler.cpp` implementa uma biblioteca C++ que compila em memória a saída dos decompiladores auditados, usando DXC v1.8.2505.1 com checksums fixados. No Windows, o verificador inclui o DLL como recurso do próprio EXE, extrai em pasta temporária exclusiva e carrega pelo caminho completo com busca restrita. Descarrega e remove a cópia ao encerrar normalmente; encerramento forçado pode deixar a pasta. As licenças do DXC e do SDK acompanham o artefato. Não exige SDK ou ferramentas instaladas pelo usuário.

O modo HLSL 2018 mantém as condições vetoriais usadas pela saída existente. Compila para Vulkan 1.1/SPIR-V, com inversão de Y no vertex shader. Define ATTR0…15 em locations 0…15, COLOR0/1 em 0/1, FOG em 2 e TEXCOORD0…7 em 3…10. Separa vertex/fragment nos descriptor sets 0/1; cbuffers usam binding bN, texturas 32+tN, samplers 64+sN e UAVs 96+uN. Arrays continuam arrays de descriptors; estes números não são contagens de descritores. Um leitor limitado de metadados SPIR-V confere locations e colisões; ele não substitui o validador SPIR-V completo.

O teste compila sete módulos próprios a partir de programas RSX sintéticos e HLSL de teste: vertex MOV, vertex TXL, fragment MOV, fragment TEX, constantes/alpha em buffer, espaços de recursos e recuperação após código inválido. Não inclui shaders ou assets do jogo. A CI Linux valida cada módulo também com `spirv-val`. A CI Windows testa o EXE sozinho em uma pasta separada e confere a limpeza temporária do DXC.

Para desenvolvedores: configure com `-DD2_VULKAN_SHADER_COMPILER=ON -DD2_RSX_SDK_DIR=<SDK auditado>`. O alvo `DisgaeaD2ShaderProbe` gera `shader-check/shader-report.json` e módulos de teste. O compilador permanece opcional e separado do probe de renderização. **Ainda não cria pipelines gráficos a partir desses shaders, não conecta seus descriptors/constantes ao renderer e não testa shaders reais do Disgaea.** A seleção Vulkan para jogar continua desabilitada.
