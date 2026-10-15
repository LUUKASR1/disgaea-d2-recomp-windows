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

Para desenvolvedores: configure com `-DD2_VULKAN_SHADER_COMPILER=ON -DD2_RSX_SDK_DIR=<SDK auditado>`. O alvo `DisgaeaD2ShaderProbe` gera `shader-check/shader-report.json` e módulos de teste. O compilador permanece opcional e separado do probe de renderização. Esse verificador isolado testa somente compilação; a etapa seguinte conecta shaders sintéticos ao renderer. **Nenhuma dessas etapas testa shaders reais do Disgaea.** A seleção Vulkan para jogar continua desabilitada.

## Shaders RSX sintéticos executados na GPU

`DisgaeaD2RsxGpuProbe` conecta o compilador auditado ao renderer reutilizável: atributos float4 em 16 posições, constantes VP (`set=0,binding=0`), constantes FP (`set=1,binding=1`), 16 imagens 2D e 16 samplers FP (`binding=32/64`). Cada unidade tem imagem independente; unidades não vinculadas usam uma imagem branca imutável. Uploads e atualizações aguardam o fence; dados host são explicitamente flushed. Os programas são registrados antes da criação do alvo e permanecem imutáveis, podendo ser selecionados por draw. Os pipelines legados continuam disponíveis em instâncias sem programa RSX.

O ABI aceita somente SPIR-V confiável produzido pelo compilador auditado e faz verificações limitadas de layout/tamanho, estágio e varyings. Não é um carregador de módulos arbitrários. Vertex textures, MRT no replay do jogo, cubemaps, outros formatos/filtros e reprodução de comandos do jogo continuam pendentes. MRT offscreen é descrito na etapa posterior. O cbuffer FP fica limitado a 16 KiB (mínimo portátil do Vulkan); programas maiores são rejeitados. O teste cobre 24 imagens de 64×48, 73.728 pixels (tolerância por canal de 1/255), incluindo escala/translação, UV, unidades independentes, blend, alfa e vida útil de uploads. A CI Linux executa lavapipe com validação de sincronização; Windows sem ICD é marcado indisponível. É necessário testar o artefato Windows na GPU do usuário.

Compile com `D2_VULKAN_SHADER_COMPILER=ON` e SDK auditado, conforme a etapa do compilador. Execute `DisgaeaD2RsxGpuProbe --output rsx-gpu-check`; para desenvolvimento, adicione `--validation`. O workflow de shaders produz o artefato experimental `DisgaeaD2-RSX-GPU-Probe-Windows-x64`, com DXC embutido e licenças. Sem arquivos do jogo, sem mudança para Vulkan no launcher e sem publicação de release estável.

## Programas e constantes por desenho (schema 2)

`setRsxProgram` registra o programa padrão 0 e configura o ABI. `addRsxProgram` registra até 32 pares VS/FP antes de criar o alvo, reutilizando handles para módulos idênticos. Cada programa tem seis PSOs (depth × blend) criados para o render pass; todos usam o mesmo pipeline layout. `DrawCommand::rsx_program` seleciona o par e `rsx_constants` seleciona os buffers desse desenho.

`createRsxConstants` copia os dados para um snapshot independente e imutável. Até 64 estados, incluindo o padrão 0, podem existir até fechar o renderer. `setRsxConstants` atualiza somente o estado 0, após o fence. A lista inteira é validada antes de adquirir a imagem ou gravar comandos: handles desconhecidos, estado incompleto ou cbuffer FP curto são rejeitados. Texturas continuam globais ao renderer; snapshots por material, reciclagem de buffers por quadro e registro de programas durante o jogo continuam pendentes. Estes limites são do teste experimental, não uma solução pronta para executar o jogo.

O verificador inclui oito imagens com três pares VS/FP (dois vertex shaders distintos) e vários snapshots no mesmo quadro: compara todos os pixels, inverte a ordem de submissão com depth, muda o padrão 0 sem alterar snapshots, substitui textura, alterna blend e testa rejeições sem modificar o alvo. Relatório: `schema=2`, `renderer_stage=per-draw-programs-and-constants`, 24 casos / 73.728 pixels, dos quais oito casos verificam o caminho por draw. A etapa anterior passou na RTX 5070 do usuário (16 casos / 49.152 pixels, sem camada Khronos); este novo schema precisa de novo teste nessa GPU.

O exemplo independente `port/vulkan/examples/win32_init.h` mostra a inicialização Win32 com `HWND/HINSTANCE`, as extensões surface/win32_surface, seleção de fila graphics/present e habilitação de swapchain no device. É compilado como objeto na CI Windows para verificar sintaxe; não é executado lá. Para integrá-lo como aplicação separada, vincule `vulkan-1.lib`. O renderer do projeto continua carregando o loader dinamicamente.

## Corpus real do jogo: coleta e triagem (launcher 0.1.46)

A etapa por draw passou na RTX 5070 do usuário: 24 casos, oito por draw, 73.728 pixels, sem camada Khronos. O próximo passo utiliza fontes reais para orientar prioridades, consultando o RPCS3 como referência de semântica RSX. Não é uma importação do backend RPCS3 nem uma comparação automatizada com sua imagem de saída.

O modo **Shaders — preparar Vulkan** observa as fontes finais após compilação D3DCompile bem-sucedida no renderer D3D12. Não solicita F8, readbacks, motion probes ou imagens. Deduplica por SHA-256 e estágio; escreve fontes e amostras binárias RSX apenas no perfil local. Fontes equivalentes compartilham a primeira amostra observada (constantes inline FP podem variar em outros desenhos). Metadados registram início do VP, controle automático do FP, cube mask e hash HLSL. Não representa snapshot de um draw nem captura todos os estados.

O coletor limita o payload a 32 MiB, 256 fontes, 256 KiB por HLSL e 64 KiB por amostra RSX; grava arquivos temporários antes de renomear. Manifesto informa erros e truncamento; `closed` indica encerramento da sessão, não cobertura completa do jogo. Com a opção desativada, o hook não exporta nem percorre bytecode para capturá-lo. O SDK auditado/lock permanece intacto; as chamadas são injetadas somente na cópia de build.

`DisgaeaD2ShaderCorpus --input shaders --output corpus-check` compila HLSL individualmente e classifica separadamente falhas de compilação e lacunas no ABI Vulkan. Continua após uma fonte inválida. Não vincula pares nem valida equivalência visual. O teste automatizado usa fontes próprias e não pode ser apresentado como validação do Disgaea. Veja `port/vulkan/shader/CORPUS-README.md`. As fontes/amostras do usuário não devem ser incluídas em commits, artefatos ou releases públicos.

## MRT offscreen após a primeira coleta real

A primeira coleta fornecida pelo usuário contém dois vertex shaders e três fragment shaders. Todos compilaram com DXC; dois fragment shaders expuseram uma lacuna concreta: saídas MRT nos locais 0–1 e 0–2. Nenhuma fonte ou amostra RSX dessa coleta foi incorporada ao repositório.

O renderer agora oferece `offscreen(width, height, color_targets)` e `readPixels(attachment)`, com 1–4 alvos RGBA8 e profundidade compartilhada. A criação valida que todos os locais de saída dos programas têm alvos vinculados; não descarta saídas para simular compatibilidade. Habilita `independentBlend` apenas quando suportado e exige esse recurso para MRT. Máscaras de escrita preservam alvos que um programa não escreve. Todos os alvos têm o mesmo formato e estado de blend por desenho; formatos heterogêneos, vínculo dinâmico de alvos e replay do estado RSX real ainda não estão implementados. Apresentação Win32 continua com um alvo.

O GPU probe schema 3 adiciona 54 leituras de alvos MRT às 24 anteriores: 78 verificações, 239.616 pixels. Testa cores distintas por saída, blend, descarte, profundidade nas duas ordens e programa de saída única após MRT. A coleta real passa na compilação e no ABI individual (5 compilados, 0 erros, 0 lacunas); ainda não há ligação dos pares reais, equivalência de pixels ou jogo Vulkan validado. O launcher permanece em Direct3D 12.

## Desenho real isolado (launcher 0.1.47)

O usuário confirmou MRT schema 3 na RTX 5070: 78 verificações, 54 MRT, 239.616 pixels, sem camada Khronos. A etapa seguinte oferece captura F8 de até quatro draws de um único alvo, com shaders associados por hash do bytecode, bancos de constantes completos e geometria expandida do slot/paridade realmente submetidos. O hook de SRV registra o recurso/view efetivos; mantém referências durante a sessão do frame. Texturas mutáveis, formatos fora do escopo e saídas FP adicionais são recusados.

O PSO original é reproduzido isoladamente sobre RGBA zero/depth 1. Os recursos originais não são alvos da reprodução; cópias de textura restauram o estado PSR. Objetos envolvidos numa submissão sem conclusão confirmada são retidos. Limites: 128 fontes/16 MiB, 128 pipelines/32 MiB, oito tentativas e quatro draws por solicitação, até oito solicitações e orçamento reservado de 256 MiB de payload da sessão. Nenhum dump acontece em lançamento normal. O SDK/lock não é modificado.

`DisgaeaD2DrawReplay` compila as fontes com DXC, aplica estado de raster/blend/depth e samplers por unidade, envia geometria/constantes/texturas e compara todos os canais do alvo 0. A tolerância é 1/255; imagem de referência vazia não conta como validação. Input incompleto elimina imagens antigas antes de falhar. Só uma coleta real poderá validar esses draws; a suíte própria não representa equivalência do jogo. Depth Vulkan é float, não o D24 original. Ver `port/vulkan/shader/DRAW-README.md` para o escopo completo. Vulkan continua desabilitado para jogar.

## Seleção de referência não vazia (0.1.48)

A primeira coleta real 0.1.47 trouxe somente um draw: a referência e o replay Vulkan ficaram totalmente zerados. O comparador retornou corretamente `empty-reference`, sem aprovar equivalência visível (0 diferenças e 0 erros de validação não bastam). O diagnóstico de apresentação também sobrescreveu o marcador dedicado com `failed`, pois procurava arquivos do formato antigo.

A finalização antiga agora preserva o marcador de desenho isolado. O coletor não conta referências zeradas como capturadas e continua a seleção, registrando `empty_references`. A reflexão admite saídas de cor 0–3, mantendo a exigência de um único alvo RGBA8 realmente vinculado. O pacote v2 preserva a máscara refletida; o leitor confere a máscara contra SPIR-V e registra as saídas sem alvo. Um opt-in explícito no raster state replica esse descarte do pipeline capturado; a validação padrão de MRT permanece estrita. Essa semântica é definida em [Khronos Shader Interfaces](https://docs.vulkan.org/spec/latest/chapters/interfaces.html). Não são criados alvos artificiais para simular compatibilidade. Pacotes v1 continuam legíveis.

Testes próprios verificam duas saídas no shader D3D12 com um alvo vinculado, imagem útil preservada, recusa de referência vazia, descarte explícito em Vulkan e rejeição de máscara alterada. Nenhum shader ou asset da coleta é incorporado ao repositório. Ainda falta uma coleta real não vazia para validar o desenho do jogo.


## Texturas comprimidas na captura isolada (0.1.49)

A coleta de batalha 0.1.48 guardou o fundo (diferença máxima 1/255 no replay local com lavapipe e validação Khronos, zero erros), mas sete tentativas foram recusadas com `texture-outside-first-replay-scope`. O diagnóstico antigo não registrava formato/propriedades: ainda não comprova qual campo recusou cada uma. Não valida cenário ou personagens.

O SDK auditado usa também BC1/BC2/BC3 UNORM. O coletor 0.1.49 e o leitor agora preservam esses blocos nativos, sem decodificação CPU ou recompressão. O pacote v3 de 160 bytes acrescenta quatro formatos e quatro remapeamentos de componentes; v1/v2 de 128 bytes continuam legíveis. `texN.bc` contém blocos compactos sem padding de readback; `texN.rgba` continua pre-remapeado. Vulkan habilita `textureCompressionBC` somente quando suportado e recusa o replay comprimido quando indisponível. Swizzles mantêm R/G/B/A/zero/one do SRV original. O renderer de jogo continua D3D12.

Recusas passam a identificar unidade, formatos do recurso/view, dimensões, flags, mip/subview e remapeamento. Isso permite distinguir BC das outras limitações, em vez de atribuir todas as recusas a compressão. Testes próprios cobrem cópia D3D12 de duas linhas de blocos BC1/2/3, padding, preservação binária e metadados; no leitor Vulkan, decodificação, swizzle, bordas parciais 5×7, tamanhos inválidos e compatibilidade legada. A coleta real atual não contém os blocos recusados; será necessária nova coleta após atualizar o launcher para confirmar a batalha.
