# Apresentação e integração temporal

## 0.1.20 — maior Hz e VRR

Em Vídeo, "Usar maior taxa de Hz" seleciona a maior frequência enumerada pelo
Windows para a resolução, orientação e profundidade de cor atuais do monitor
onde a janela nasceu. Não altera a resolução do desktop, não grava no registro
e testa o modo antes de aplicá-lo. A frequência original é restaurada ao fechar
normalmente ou no atexit, desde que o usuário não tenha alterado o modo depois.
Encerramento forçado pode impedir a restauração; reconfigure o Hz no Windows.

A opção é desligada por padrão. O log D2DISPLAY registra Hz atual, maior Hz do
mesmo modo e resultados da troca/restauração. O launcher reutiliza saves e build.
Mover a janela a outro monitor durante a sessão não troca automaticamente o modo.

VRR também é opt-in. A criação do swapchain consulta IDXGIFactory5 para
DXGI_FEATURE_PRESENT_ALLOW_TEARING. A flag de Present só é usada com intervalo 0,
swapchain criado com ALLOW_TEARING e modo de janela, inclusive sem bordas.
VSync ligado suprime a flag. Suporte do driver e VRR habilitado no Windows/monitor
são necessários; capacidade de tearing não comprova que o monitor possui VRR.
Isso permite apresentação de alta frequência, mas não cria frames adicionais
nem altera o relógio/simulação do jogo. Hz e FPS são medidas diferentes.

## DLSS Super Resolution e DLSS Frame Generation — pendentes

Documentação primária consultada em 14/10/2026:
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md
- https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays

A cor e profundidade já existem, assim como uma cena sem HUD nos frames
reconhecidos. Antes de chamar DLSS, precisamos produzir e validar:

1. Matrizes de câmera atuais e anteriores sem jitter, near/far, convenção de depth,
   frame token, delta de tempo, cortes de câmera e reset de histórico.
2. Motion vectors R16G16_FLOAT da câmera, personagens e demais objetos dinâmicos,
   com correspondência entre frames. Não aceitar vetores zero como integração.
3. Jitter de projeção aplicado ao rasterizador e removido dos vetores/matrizes;
   política de exposição e recursos de cor/saída corretos para DLSS SR.
4. Cena sem HUD na resolução da saída válida até Present para FG. Fallbacks sem
   separação devem desabilitar FG em vez de interpolar a UI misturada.
5. Streamline oficial assinado, detecção de suporte do adaptador, DLSS SR antes da
   HUD, Reflex com marcadores de simulação/render/Present e swapchain proxy FG.
6. Desativar FG em loading/menus/pausa e recriar swapchain ao ligar/desligar.
7. Medir FPS renderizado, FPS apresentado/gerado, latência e frametime separadamente.

O próximo trabalho do renderer será obter histórico real da câmera e dos objetos.
A tradução de shaders do PS3 não oferece esses dados automaticamente. Apenas
liberar controles ou copiar DLLs não implementa DLSS. Os controles continuarão
indisponíveis até o pipeline fornecer entradas válidas e executar a tecnologia.

Para o desejo de letras mais suaves, comparar HUD na saída e HUD já ampliada;
a integração temporal deve preservar UI e não assumir que o caminho separado
já tem a melhor filtragem visual para todos os menus.

## 0.1.21 — seleção de Hz e apresentação sem cap

Vídeo oferece Nativo, 60, 75, 120, 144, 165 e 180 Hz. Nativo mantém o modo atual
do Windows. Uma frequência numérica só é aplicada quando o driver a enumera
para a mesma resolução/orientação/cor e CDS_TEST a aceita; caso contrário,
mantém a atual e registra o motivo. A antiga opção maximum_refresh é descartada
na migração para Nativo. A restauração continua limitada ao encerramento normal.

Sem limite de FPS é habilitado por padrão nesta preview, como solicitado pelo
usuário. D2_GCM_UNLIMITED remove o requisito de esperar um novo vblank antes de
apresentar um flip concluído e o recheck usa a mesma regra antes de dormir.
O relógio monotônico de 60 Hz, os contadores e notificações vblank não são
alterados. FIFO completo, posse do ticket e aposentadoria do frame continuam
obrigatórios. O modo antigo pode ser restaurado desligando Sem limite de FPS.

O SDK original e seu lock ficam intactos. adapt_pacing.py audita o SHA-256 do
cellGcmSys.c conhecido e gera uma cópia de build. Nenhum limite de FPS adicional
é criado pelo launcher. VSync ainda pode limitar a apresentação à taxa do monitor.

Callbacks de flip podem afetar jogos que usam tempo fixo por frame. Preservar
o relógio de vblank não prova que todas as animações/lógica do jogo são independentes
dos flips. Por isso, validar movimentação, animações, áudio, batalha, save/load e
fechamento; se acelerar, restaurar o modo antigo. A preview não promete FPS acima
de 60 se o próprio jogo ou a carga de CPU/GPU limitarem a produção dos frames.

A integração de DLSS/FG segue a próxima etapa: validar câmera e movimento reais,
depois Streamline DLSS SR, depois Reflex/swapchain FG. Não são habilitados nessa versão.

## 0.1.22 — diagnóstico temporal e ritmo original

O usuário constatou aceleração do jogo com apresentação sem limite na 0.1.21.
A configuração agora migra fps_unlimited para false, sobrescreve o ambiente
e remove o controle ativo da interface. Hz e VRR continuam disponíveis. A cópia
auditada do GCM conserva o caminho de 60 Hz quando D2_GCM_UNLIMITED=0.

Com capturas habilitadas, a cada frame apresentado o diagnóstico copia até
quatro bancos de constantes VP de draws da cena em dois slots de memória
(aproximadamente 66 KB no total). Não há readback GPU, disassembly ou gravação
contínua. F8 gera temporal-probe.json junto aos arquivos anteriores.

A captura reporta o frame atual e anterior apresentado, hashes de shader,
draw/FP/contagem de vértices/textura, slot de constantes e os registros de
cb0 usados pelo DXBC do shader, obtidos por D3DDisassemble. A declaração de banco
não é considerada leitura; referências indiretas são sinalizadas, e um shader
ausente/evictado não recebe constantes inventadas. Valores não finitos viram null.
As duas paridades e o cb_slot real são respeitados.

Nenhuma matriz é classificada como câmera nessa etapa. Ordem dos draws não
identifica objetos, registros lidos podem conter iluminação/viewport e o delta
de observação CPU não é tempo de simulação. motion_vectors_available,
camera_matrices_identified e object_identity_available permanecem false.

Para diagnosticar: 720p interno/saída, upscaling desligado, captura habilitada.
No Castle Hallway, mover/girar a câmera enquanto pressiona F8. Na batalha,
capturar um personagem em movimento e um golpe. Enviar temporal-probe.json
de cada situação. Se uploads falharem, colar os JSONs diretamente. Essas
capturas identificam constantes candidatas; a próxima etapa valida a transformação
de posição, near/far e histórico dos objetos antes de criar motion vectors.

F8 também grava vp_<hash>.asm com o disassembly do shader disponível, apenas
na pasta de captura do usuário. Isso permite verificar quais registros alimentam
a saída de posição; ler um registro não demonstra que ele seja uma matriz de câmera.
Não há shaders/código do jogo incluídos no repositório ou no launcher.

Cada amostra inclui os 16 atributos float4 do primeiro vértice, quando o offset
e a memória são válidos. Isso permite avaliar a transformação de posição com
entradas reais do mesmo draw; não fornece identificação persistente do objeto.

## 0.1.23 — regra de posição e coleta de um par por F8

O disassembly enviado pelo usuário mostra a saída de posição de VP 99362655:
produto dos atributos de posição com c256–c259, ajuste c466.z, escala c512 e
offset c513. A implementação preserva a correção xy pelo quociente bias/w,
w e z ajustados pelo bias e a escala/offset de saída. Pontos singulares,
atrás da câmera e resultados não finitos são rejeitados.

projection-check.json é um relatório compacto com clip e deslocamento de
um mesmo ponto sob a transformação anterior e atual, em pixels de renderização.
O sinal usa UV anterior menos atual, com eixo Y para baixo. A correspondência
por shader/FP/textura/contagem não é identidade de objeto. Esse deslocamento
não é um campo de motion vectors, nem implementa movimento de personagens.

A coleta temporal deixa de copiar bancos em todos os frames. F8 arma o par:
o primeiro frame apresentado copia as amostras; o segundo copia as suas
amostras e recebe a captura/imagens, dump e marcador de conclusão.
A gravação/disassembly/readbacks ficam depois das duas observações. Cada
requisição consome um único índice do limite de oito capturas. Sem requisição
pendente não há cópia temporal. Capturas desabilitadas mantêm esse diagnóstico
inativo. F8 ainda pode causar uma pausa pelos readbacks; o custo restante do
modo de captura precisa ser medido, pois ainda conserva metadados RSX.

O usuário observou lentidão com capturas ligadas e normalização ao desligá-las
na 0.1.22. Isso associa o efeito ao modo diagnóstico, mas não prova que a cópia
temporal fosse a única causa. A redução deve ser validada no jogo real.

Testes comparam a função CPU com uma referência HLSL no D3D12 WARP, usando
identidade, translação, rotação e correção de bias. Também verificam sinal/escala
de UV, deslocamento nulo e rejeição de singularidades. Não são uma comparação
com o resultado renderizado do shader proprietário do jogo.

Para continuar: 720p interno e saída, upscaling desligado, capturas habilitadas.
Girar a câmera e pressionar F8 durante o movimento. Enviar projection-check.json;
o relatório detalhado e vp_99362655.asm ficam disponíveis para a análise.
DLSS e FG seguem indisponíveis enquanto faltam câmera e movimento completos.

## 0.1.24 — F8 espera por mudança de transformação

Com captura habilitada, pressione F8 primeiro e depois gire a câmera dentro
de dez segundos. A coleta fica armada, mantendo apenas o par mais recente,
até encontrar mudança significativa dos registros 256–259 de um draw 99362655
com mesma assinatura e primeiro ponto inalterado. Ruído pequeno e valores
não finitos não selecionam um par. Isso detecta mudança de transformação;
não prova identidade de objeto nem movimento exclusivo da câmera.

Enquanto aguarda, copia somente sete float4 necessários ao shader conhecido
(256–259, 466, 512 e 513), os atributos do primeiro vértice e metadados.
Para outros shaders conserva apenas 512/513. Os registros ausentes não são
serializados como zeros: temporal-probe.json informa captured_rows e
capture_scope=transform-only. Não existem leituras GPU ou gravações nesse loop.

Ao selecionar, congela o par antes dos dumps/readbacks. A captura das imagens
é feita no frame apresentado seguinte; capture_image_frame identifica esse
frame separado de current_frame/previous_frame da observação selecionada.
A requisição mantém um único índice e marcador de conclusão.

projection-check.json inclui selection_status=transform-changed ou
timeout-no-transform-change. No prazo expirado captura o diagnóstico sem
afirmar sucesso; não espera indefinidamente. Se o jogo fechar antes de completar
a coleta, não há garantia de relatório completo. Aguarde a captura terminar.

Testes cobrem espera estática, seleção/congelamento, emissão única, prazo
exato de dez segundos e rejeição de ruído/NaN. A seleção e seu desempenho
no jogo real continuam pendentes. F8 ainda pode causar pausa pelos readbacks
posteriores. DLSS e Frame Generation continuam indisponíveis.

## 0.1.25 — validação de amostras na GPU

O usuário confirmou a seleção automática na 0.1.24: um par consecutivo com
transformações alteradas, primeiros pontos inalterados e deslocamentos não
nulos nas duas amostras. Isso não comprova câmera suave, ausência de cortes
ou correspondência de todos os vértices; pontos podem estar fora da tela.

A captura agora executa um compute shader no dispositivo e fila D3D12 do jogo.
Ele transforma o mesmo ponto com os dois bancos congelados e calcula o delta
em pixels, com UV anterior menos atual. motion-gpu-check.json compara o
resultado com a referência CPU, tolerância de 0,25 pixel, e identifica os
frames observados. Uma assinatura ambígua de primeiro ponto é rejeitada.
O diagnóstico não altera os recursos, command list ou apresentação do jogo.

Compilação, dispatch e readback só ocorrem após uma captura selecionada.
Não há execução contínua, textura de vetores, cobertura rasterizada ou
identidade de personagens nessa versão. Uma pausa na captura é esperada.
Falhas de GPU ficam registradas e não mudam o render normal. Em timeout de
30 segundos ou falha de sinalização após submissão, os recursos do diagnóstico
são retidos até o fim do processo para evitar liberar memória em uso pela GPU.

Testes D3D12 WARP usam lotes de 65 amostras para conferir o limite de dispatch,
posição estática, translação, rotação, bias, sinal dos vetores e rejeição de
w singular/negativo, NaN e dimensão zero. São testes da matemática do
diagnóstico; não equivalem a medir a saída do shader original do jogo.

Para testar, mantenha 720p interno e saída e upscaling desligado. Habilite
capturas, pressione F8 e gire a câmera em até dez segundos. Após concluir,
envie motion-gpu-check.json. Desabilite capturas para jogar normalmente.
DLSS e Frame Generation permanecem indisponíveis. O próximo passo é rasterizar
candidatos de cenário com máscara de cobertura e comparar profundidade, direção
e oclusões; o movimento de sprites/personagens exige correspondência própria.

## 0.1.26 — raster de candidatos estáticos do cenário

A captura 0.1.25 enviada pelo usuário confirmou cpu-gpu-match nas duas amostras,
com erro máximo aproximado de 0,000057 pixel. Isso valida os pontos medidos
na GPU do usuário, não o campo completo, a identidade ou os cortes de câmera.

Enquanto F8 aguarda a seleção, guarda posições completas de até duas amostras
99362655, com limite de 4096 vértices por amostra. Aceita somente listas de
triângulos com viewport completo, sem blend, alpha test ou stencil. Overrides
CULL_OFF/CULL_FLIP são rejeitados. A cópia permanece limitada à janela armada.
Para rasterizar, exige geometria inteira idêntica, assinatura/cull coincidentes
e correspondência única no par. Isso é uma hipótese estática conservadora;
não fornece identidade persistente de objeto.

As posições são transformadas pela regra CPU validada. Um VS/PS D3D12 próprio
rasteriza esses candidatos num alvo RGBA32_FLOAT com depth D32 e comparação
LESS_EQUAL. O shader interpola as posições homogêneas atual e anterior em perspectiva
e calcula UV anterior menos atual por pixel. Z do alvo sinaliza cobertura
e W registra profundidade. O teste de profundidade resolve somente oclusões
ENTRE OS CANDIDATOS, com culling equivalente aos bits registrados.

Não lê nem modifica depth/cor/texturas da cena. Não reproduz alpha discard do
fragment shader, stencil, depth bias ou estados de profundidade originais.
Personagens e draws não amostrados podem ocultar essa geometria no jogo;
por isso full_scene_visibility_validated e motion_vectors_available são false.
É um diagnóstico parcial, sem integração com DLSS/FG. Cortes, disoclusões
entre frames e histórico de sprites permanecem pendentes.

motion-raster-check.json informa frames do par, frame separado das imagens
normais, candidatos rejeitados, cobertura, pixels com movimento e valores
máximos. motion-coverage.bmp mostra a máscara em branco; motion-vectors.bmp
codifica X em vermelho e Y em verde, zero próximo de 128, faixa visual
de -256 a +256 pixels com saturação. As imagens vêm do par congelado,
não necessariamente do frame que gerou output.bmp. A faixa visual não
limita nem clampa os valores calculados ou reportados no JSON.

Não há raster ou readback contínuo: cria/compila/dispatch/readback somente
no dump da captura. Pode causar pausa. Testes WARP cobrem interpolação
perspectiva, sinal/escala, máscara fora dos triângulos, posição estática,
profundidade independente da ordem e culling/front-face. Falhas após
submissão retêm recursos até o fim do processo, como o diagnóstico anterior.

Teste com 720p interno/saída e upscaling desligado. Habilite capturas, F8,
gire a câmera em dez segundos e aguarde finalizar. Envie motion-raster-check.json;
as duas BMPs ajudam a verificar cobertura e direção visual. Desabilite
capturas para jogar normalmente. Nenhum recurso derivado do jogo é publicado.

## 0.1.27 — prioridade de geometria elegível e motivos de rejeição

O relatório real 0.1.26 rejeitou ambas as amostras antes do raster; zero draws
não foi falha da GPU. A versão anterior escolhia os primeiros draws do shader
mesmo quando não eram elegíveis para esse diagnóstico. O motivo exato naquele
par não está disponível e não deve ser inferido.

A observação agora prioriza até duas amostras com geometria elegível, depois
completa as demais amostras do diagnóstico de pontos. Todos os filtros continuam
ativos. Se nenhuma geometria for elegível, conserva amostras rejeitadas para
explicar o motivo. Não garante encontrar geometria estática ou cobertura.

motion-raster-check.json inclui sample_rejections por draw: motivo, quantidade
de vértices copiados, número de correspondências completas, estado de blend,
alpha test, stencil, topologia e viewport. eligibility_mask soma os bits:
1 shader, 2 entrada ausente/offset inválido, 4 viewport parcial, 8 topologia,
16 limite de 4096 vértices, 32 intervalo de memória, 64 blend, 128 alpha test,
256 stencil, 512 override de culling. Zero permite copiar geometria; o pareamento
completo e a projeção ainda precisam passar. O JSON distingue geometria
inelegível, correspondência não única/alterada e projeção inválida.

Use o mesmo teste F8 em 720p e envie o novo JSON. DLSS/FG permanecem pendentes.

## 0.1.28 — cenário com alpha/blend no diagnóstico geométrico

O JSON 0.1.27 enviado pelo usuário identifica eligibility_mask=192 nas duas
amostras: blend (64) e alpha test (128). Topologia, viewport e stencil passam.
Não é falha da GPU e não informa se os texels visíveis são opacos.

Esses dois estados deixam de bloquear a cópia/raster de GEOMETRIA CANDIDATA.
Os demais filtros estruturais e pareamento completo continuam obrigatórios.
O bitmask original é mantido e cada draw informa
accepted-geometry-only-alpha-blend-unreplayed quando esses estados estão ativos.
O renderer normal não é modificado; o diagnóstico continua isolado.

O relatório explicita coverage_scope=candidate-geometry-only,
shader_alpha_test_replayed=false, blend_replayed=false e a quantidade de draws
com esses estados não reproduzidos. A máscara branca é uma silhueta geométrica
possível, não a contribuição final do fragment shader. Pixels que o shader
descarta podem aparecer nela; depth LESS_EQUAL entre candidatos não resolve
transparência. Não usar essa máscara para ativar DLSS ou geração de frames.
full_scene_visibility_validated e motion_vectors_available continuam false.

O próximo passo após verificar direção/cobertura geométrica é reproduzir
alpha/discard e a contribuição dos draws reais, incluindo oclusões por
personagens. Essa versão não afirma que isso esteja implementado.

## 0.1.29 — candidatos no frustum e diagnóstico de culling

O relatório real 0.1.28 aceitou dois draws e 78 vértices, sem rejeições ou erros
GPU, mas não cobriu nenhum pixel. Isso não comprova falha da projeção: os
candidatos podem estar fora do frustum ou ser descartados pelo culling.

A seleção agora prioriza geometria com pelo menos um triângulo potencialmente
visível no frustum D3D (x/y entre -w e w, z entre zero e w). O teste rejeita
triângulos inteiramente além do mesmo plano e degenerados; aceita triângulos
que cruzam a tela mesmo que nenhum vértice esteja dentro. Não prova cobertura
de pixels ou oclusão. Valores singulares/atrás da câmera continuam conservadores.
Bits adicionais: 1024 sem triângulo candidato, 2048 projeção inválida.

O JSON registra cull, quantidade de triângulos candidatos e limites NDC das
geometrias aceitas. Quando o raster principal fica vazio e há culling ativo,
executa uma comparação separada com culling desligado e reporta se ocorreu,
resultado GPU e pixels cobertos. Essa comparação não substitui as imagens
principais, não modifica o jogo e não habilita DLSS. Só ocorre na captura vazia.

Um teste nativo adicional cobre os seis planos, triângulos cruzando o viewport,
degenerados, NaN e w negativo. A validação real da nova seleção está pendente.
Alpha/blend, visibilidade completa, identidade e motion vectors utilizáveis
continuam pendentes. Use o mesmo F8 em 720p e envie motion-raster-check.json.

## 0.1.30 — referência visual do frame selecionado

O usuário confirmou um raster 0.1.29 com 699 vértices, 11.221 pixels cobertos
e em movimento, zero inválidos. O segundo draw não teve geometria inteira
correspondente. As imagens de máscara e vetores ocupam a mesma pequena região
superior esquerda. output.bmp, porém, era do frame seguinte; não se confirmou
alinhamento preciso ou direção visual a partir dessas imagens.

Após submeter o frame e selecionar/congelar o par, o adaptador agora faz um
readback do alvo interno completo ainda correspondente ao current_frame.
Grava selected-reference.bmp antes de renderizar outro frame. O helper de
readback existente sincroniza a fila, copia/restaura o estado PRESENT e confirma
a escrita pelo tamanho/header/dimensões. O arquivo é truncado antes da tentativa
para uma falha não reutilizar uma imagem antiga. É cor interna completa,
incluindo HUD: não é a imagem de saída ampliada e não é uma cena sem HUD.

O JSON de raster inclui reference_image, reference_image_frame (null em falha),
reference_image_available, reference_matches_current_frame e reference_space.
As imagens normais output.bmp continuam vinculadas a capture_image_frame,
possivelmente seguinte, e não devem ser usadas como referência alinhada.

O prazo de dez segundos passa a expirar na observação após o render, não na
requisição anterior ao render. Assim, também no timeout, a geometria congelada
e a referência vêm do mesmo frame. O dump normal continua na requisição
seguinte, uma única vez; uma seleção não provoca readback contínuo. O teste
automático cobre o deadline pré-render, observação pós-render e emissão única.

Use 720p interno/saída, upscaling desligado, captura habilitada; F8 e giro da
câmera em dez segundos. Aguarde concluir e envie motion-raster-check.json,
selected-reference.bmp e motion-coverage.bmp. Falha da referência é explícita;
o diagnóstico geométrico pode continuar disponível. Readbacks podem pausar o
jogo. Alpha/blend, oclusões completas, identidade, DLSS e FG seguem pendentes.

## 0.1.31 — profundidade final do mesmo frame

O usuário confirmou referência alinhada no frame 1409, dois draws e 1575
vértices, 16.141 pixels cobertos/em movimento e zero inválidos. Máscara e
vetores correspondem às mesmas pequenas regiões nas bordas. A captura do
mesmo frame está funcional; direção temporal e visibilidade completa não
foram comprovadas por uma imagem.

Ao selecionar o frame, o diagnóstico agora lê o plano 0 do depth D24/R24G8
do jogo na mesma fila. Usa GetCopyableFootprints e copia a subresource inteira,
conforme exigido para depth/stencil. O buffer D24X8 tem profundidade nos bits
0–23; normaliza esses bits para float, sem confundir stencil com depth.
A CPU recorta apenas após o readback para as dimensões internas, no canto
superior esquerdo do depth maior do SDK. As barreiras restauram DEPTH_WRITE
do plano 0. Não cria SRV nem altera formato ou conteúdo da profundidade.

Fontes primárias consultadas em 14/10/2026:
- https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion
- https://microsoft.github.io/DirectX-Specs/d3d/PlanarDepthStencilDDISpec.html

Compara depth do candidato ao depth FINAL do mesmo frame, tolerância de oito
níveis D24 (aproximadamente 4,77e-7). O JSON informa captura/frame/resultado,
contagens de depth coincidente, candidato mais distante, candidato mais próximo
e valores inválidos. motion-depth-classification.bmp usa verde=coincidente,
vermelho=mais distante, azul=mais próximo, magenta=inválido e preto=fora da máscara.

Essas classes NÃO comprovam visibilidade. Blend, alpha/discard, escrita de
depth desligada, depth bias, estados de comparação e HUD podem afetar a leitura
final. Coincidência pode pertencer a outra superfície; um candidato próximo
pode não ter contribuído para depth. depth_visibility_validated e visibilidade
completa continuam false. O alpha/discard original ainda não é reproduzido.
As imagens/vetores geométricos anteriores ficam preservados sem filtragem.

Testes WARP cobrem D24 typed/typeless, source maior que o recorte, row pitch
alinhado, stencil A5, profundidades .25/.5/1 e restauração para outro clear.
Também cobrem tolerância/categorias/NaN. Executa somente em captura solicitada,
pode pausar e reporta falhas; em falha após submissão mantém recursos e uma
referência ao depth para não liberar memória em voo.

Repita o F8 em 720p e envie motion-raster-check.json e
motion-depth-classification.bmp com selected-reference.bmp. Primeiro validar
a comparação de depth; depois reproduzir alpha/discard dos shaders reais.
DLSS SR e Frame Generation continuam indisponíveis.

## 0.1.32 — máscara diagnóstica de depth coincidente

O caso real 0.1.31 no frame 2922 teve 7749 pixels: 2268 coincidentes e 5481
mais distantes, sem inválidos ou candidatos mais próximos. As regiões verdes
coincidem visualmente com trechos de pequenas muretas na referência alinhada.
Isso suporta a comparação nesses candidatos, não prova visibilidade completa.

O diagnóstico produz agora motion-depth-filtered-coverage.bmp e
motion-depth-filtered-vectors.bmp, preservando somente candidatos com vetores
finitos e depth coincidente dentro da mesma tolerância D24. Mais próximos,
mais distantes e inválidos ficam sem cobertura. Vetor zero estacionário continua
válido quando a máscara é 1; pixels rejeitados têm máscara zero e NÃO devem
ser interpretados como vetores zero válidos.

O JSON informa aplicação, política coincident-only, contagens mantidas/rejeitadas
e escrita das imagens. A falta de depth/alocação não gera imagens filtradas.
A máscara coincidente não comprova alpha, blend, identidade ou temporalidade:
depth_filter_is_visibility_proof e motion_vectors_available permanecem false.
Os arquivos originais de geometria e a classificação continuam disponíveis.

Para preparar a reprodução de alpha, cada amostra registra também o float4
real da linha 4 do banco FP (b1), no cb_slot e parity selecionados.
Não infere o alpha dos texels a partir do alpha_ctl. O replay ainda precisa
usar o shader e seus atributos, texturas, constantes e operações discard
originais. Esses estados não estão implementados no raster simplificado.

O teste de depth foi ampliado para garantir preservação do vetor coincidente,
rejeição de candidatos mais distantes/próximos e NaN, cobertura desconhecida,
vetor estacionário zero válido e operação in-place. Tudo continua restrito a F8.
O render normal e os controles DLSS/FG não mudam.

Para testar: mesmo F8 em 720p. Enviar motion-raster-check.json,
motion-depth-filtered-coverage.bmp e selected-reference.bmp.
Verificar contagens e sobreposição antes do replay de alpha/discard original.

### 0.1.33 — original shader alpha/discard replay (diagnostic)

The 0.1.32 game capture retained 21,144 coincident-depth pixels and rejected
5,390 deeper pixels. Alpha/discard remains independent of that depth test.
This preview adds `alpha-replay.json` and `alpha-draw-N-coverage.bmp` for each
selected world draw. The replay runs in the selected frame, before the next
frame can overwrite the descriptors, vertex upload or constant slots.

While F8 selection is armed, the adapter retains the **actual submitted** guest
fragment pipeline. It does not rebuild a shader from potentially changed guest
memory. The replay binds the original SRV window, root signature, full vertex
attributes and VP/FP constant slots, using isolated RGBA8 and D24 targets.
A pixel is a survivor when the shader writes depth below the cleared value of
1, including shaders that output black or zero alpha. LESS/LESS_EQUAL, depth
write ALL, stencil disabled and regular immutable texture inputs are required.
Unsupported depth state, fallback pipelines, mutable render-target inputs and
possible subviewport inputs are reported explicitly; they cannot fabricate a
successful alpha mask.

These are **per-draw shader-survival masks**, not a final scene visibility mask.
They are not yet intersected with the motion raster: that requires preserving
which candidate draw owns each motion pixel. Blending over the real scene,
stencil occlusion, dynamic object identity, complete scene coverage, temporal
jitter/reset and DLSS/FG integration remain pending. The existing depth-filter
images keep their 0.1.32 meaning. Normal gameplay has no extra replay/readback;
F8 capture can pause while the diagnostic GPU work completes.

Validation adds a WARP shader test with discard, a uniform toggle and an empty
draw. The shader writes black for every survivor, so color-based coverage would
fail the test. Windows CI also compiles the adapter against the unchanged,
audited SDK. For the game test, rotate the camera after F8 and inspect the new
JSON and per-draw images alongside `selected-reference.bmp`.

### 0.1.34 — per-draw coverage and three-way depth comparison

Game capture 4152 in 0.1.33 submitted draws 39/52. Draw 39's original shader
mask was empty; draw 52 retained 56,304 pixels and matched the central platform's
outline. The combined geometry also covered 56,304 pixels but only 50 were
coincident with final scene depth. Matching counts alone cannot establish
pixel equality or identify the cause of the empty draw.

This preview writes `motion-draws.json` for the frozen frame. Each accepted
static geometry pair is rasterized separately, with its own coverage image and
scene-depth classification. The combined raster's coverage channel now holds
its winning draw index + 1 (zero means uncovered). This tag is scoped to one
capture/call, never persistent object identity; the existing coverage images
remain binary, and depth-filtered diagnostic coverage remains normalized to 1.

The selected-frame original shader replay retains both its mask and D24 depth
until the raster report is written. Exact pixel overlap, geometry-only pixels
and original-shader-only pixels are counted. On overlapping pixels, projected
geometry depth is compared to the original shader's depth; each depth is also
compared independently to final scene depth. This separates projection errors
from later depth writes without declaring either the cause in advance.

New files per draw N:
- `geometry-draw-N-coverage.bmp`: binary isolated geometry coverage.
- `geometry-draw-N-depth-classification.bmp`: green coincident, red deeper,
  blue nearer than final scene depth.
- `geometry-alpha-draw-N-difference.bmp`: white overlap, red geometry only,
  green original shader only. Red is a coverage difference, not proof of alpha
  discard. Original shader comparison is explicitly unavailable on failure or
  when the frozen frame/draw identities do not match.

A zero-coverage draw is also rasterized without culling to distinguish a face
orientation issue from a sample producing no pixels even without culling.
These diagnostic replays only occur during F8 captures. They do not change
normal rendering or feed DLSS/FG. Full visibility, blend contribution, dynamic
object identity and complete scene motion remain pending. The audited SDK patch
and lock remain unchanged.

GPU validation checks winning draw ownership after overlapping draw reordering,
original shader D24 depth versus its discard mask, and clear depth on an empty
draw. The same Windows CI compiles the renderer adapter and packages the preview.

### 0.1.35 — depth timeline inside the selected render frame

In game capture 6520, projected and original shader coverage/depth matched
exactly for draws 39 (81 pixels) and 40 (133 pixels). Every survivor was deeper
than final scene depth by up to approximately 0.008924. That does not establish
whether the cause is pre-existing occlusion, a later scene draw, or a HUD write.

F8 selection now records up to three GPU-local D24 depth-plane snapshots on the
original render command list: immediately after each of the two selected world
candidate draws, and immediately before the recognized final HUD block. Source
resource state is restored after each copy. CPU readback happens only when the
frame is frozen. GPU snapshots are reset only after the renderer's existing idle
fence; frozen data is retained through the next-frame dump. Frame and draw guards
prevent the next frame from replacing a selected snapshot.

`depth-timeline.json` compares original shader survivor depth with after-draw,
middle-stage and final depth. The middle stage is before-HUD when recognized,
otherwise end-of-command-list. It counts changes on those same survivor pixels
between the three moments. Comparisons are explicitly unavailable when candidate
selection does not match or a copy/read fails. An unrecognized HUD boundary
uses the separately labeled command-list-end snapshot. A difference is not automatically labeled a HUD defect or alpha
failure: depth can already be closer at the instant the selected draw executes.

This remains capture-only diagnostics. It does not change normal rendering,
remove depth writes or feed DLSS/FG. Up to three full-source GPU snapshots are
allocated/reused; capture mode can temporarily affect performance. Tests extend
the existing WARP D24 suite with sequential .8/.4/.2 snapshots followed by a .05
final clear, typed/typeless formats, cropped readback, duplicate-slot rejection,
resource-state restoration and safe reuse after GPU completion. The audited SDK
patch and lock remain unchanged.

### 0.1.36 — pre-HUD depth for candidate motion filtering

Game capture 1721 recognized a final HUD block at draw 214. Draw 1's 61,481
original shader pixels all matched after-draw depth; 5,047 became deeper before
HUD and none changed across HUD. Draw 2's 888 pixels all matched after-draw
 depth; 715 became deeper before HUD, then all 888 changed across the HUD block.
Only 173 of that draw's original survivor pixels remained coincident before HUD;
none remained coincident in final depth. These are per-draw observations, not a
prediction of combined raster counts or proof of full scene visibility.

The candidate motion depth filter now consumes **only** a successfully read
snapshot before a recognized final HUD block, with exact frozen-frame and raster
size matches. Final depth and the unclassified command-list-end snapshot cannot
be substituted. If pre-HUD depth is unavailable, `depth_filter_applied` stays
false. No guest depth-write state is changed.

`motion-raster.json` explicitly labels `depth_filter_source`, the pre-HUD frame,
pre-HUD depth relation counts, and the number of candidates kept before HUD that
final depth would reject. Existing `candidate_depth_*` counters and
`motion-depth-classification.bmp` remain final-frame diagnostics, labeled as
such. `motion-pre-hud-depth-classification.bmp` shows pre-HUD relations (green
coincident, red deeper, blue nearer). `motion-depth-filtered-coverage.bmp` and
`motion-depth-filtered-vectors.bmp` now use verified pre-HUD depth. Per-draw
original-shader and depth-timeline diagnostics remain available.

WARP tests verify that a .2 scene-depth snapshot retains a motion sample despite
later .05 final depth, which would reject that same sample. Selection tests reject
unknown HUD boundaries, failed/missing readback, stale frames and mismatched
raster dimensions. The audited SDK lock/patch are unchanged. This is still a
capture-only partial candidate field; original alpha-mask integration, full
visibility, scene-wide motion, persistent object identity and DLSS/FG remain
pending. Normal rendering is preserved.

### 0.1.37 — owning-draw original shader coverage plus pre-HUD depth

The 0.1.36 game capture 1263 retained 52,877 candidate pixels with pre-HUD depth,
including 20,210 that final depth would reject. Draw 54 had no unique unchanged
geometry pair and remained excluded. We continue on DirectX 12 toward temporal
inputs before starting a separate Vulkan backend.

The new `motion-alpha-depth-check.json` and
`motion-alpha-depth-filtered-{coverage,vectors}.bmp` combine the verified pre-HUD
depth filter with original shader coverage. For each candidate pixel, the raw
raster's owning draw index selects **only that draw's** original shader mask and
D24 depth. The snapshot must match the frozen frame, draw and raster dimensions.
A survivor also requires projected depth to coincide with the original shader's
depth within the existing eight-D24-LSB tolerance. Unknown masks, invalid owners,
nonfinite motion, shader coverage rejection and depth mismatches fail closed.

Masks are never unioned. A discarded nearest candidate is not reassigned to a
farther candidate: recovering that layer requires a more complete visibility
implementation. Valid zero-motion pixels retain coverage; rejected pixels are
uncovered/unknown, not valid stationary vectors. The prior pre-HUD-only images
remain available for comparison. The new report includes aggregate/per-draw
kept and rejected counts, matched mask availability and the partial-field scope.
`alpha_rejected_pixels` denotes missing original shader coverage, not independent
proof that alpha discard alone caused a coverage difference.

Tests cover overlapping owners with different masks, original depth mismatch,
missing masks, bounds, nonintegral/out-of-range/nonfinite owner tags, nonfinite
vectors and valid stationary motion. Windows CI adds this suite to the existing
13 graphics suites and compiles the unchanged audited SDK through its adapter.
Normal rendering is preserved. The selected static candidate field remains a
capture diagnostic: full scene visibility, blend contribution, dynamic sprites,
complete scene motion, temporal jitter/history/reset and DLSS/FG are pending.

### 0.1.38 — expanded bounded scenery capture and symmetric pairing

Game capture 1911 validated the combined filter for draw 52: 51,501 retained
pixels, 7,673 pre-HUD-depth rejections, and no additional original shader mask,
depth, owner or motion errors. We now expand sampling from two to **up to eight**
world-shader draws, with twelve total diagnostic sample slots and the existing
4,096-vertex limit per draw. Eligible in-frustum geometry keeps priority.

Pairing now requires full geometry/pipeline-signature uniqueness in **both**
captured frame sets. Two current instances cannot borrow one previous sample;
previous duplicates, changed vertex data, texture or culling remain rejected.
Draw ordering is not identity. Automatic camera-change selection and CPU/GPU
point probes use the same conservative pairing rule. These are bounded sample
sets, not a global object registry or persistent identity proof.

Original shader coverage is replayed only for eligible unique static pairs.
Retained mask/depth snapshots have a 128 MiB budget: an oversized expansion is
reported as `snapshot-budget-exceeded`, never substituted with another draw's
mask. The per-draw depth timeline still snapshots only its original two world
candidates; the globally captured pre-HUD depth remains usable for every
accepted world sample. Missing after-draw timeline snapshots are explicit.

`motion-alpha-depth-check.json` adds world records seen, world samples captured,
limits, pairing scope and selected-candidate screen coverage percentage. This
percentage is the area covered by the filtered diagnostic field, not a measure
of complete scene correctness. Per-draw rejection reports distinguish current
ambiguity from missing/ambiguous previous geometry. Previous diagnostic image
names and owner/mask/depth rules are preserved.

Tests cover reordered samples, duplicates in either frame, full geometry changes
with an unchanged first vertex, texture/culling differences and ineligible
samples. Windows CI adds this suite to the prior fourteen graphics suites and
compiles the audited renderer adapter. Normal rendering and the SDK lock/patch
are unchanged. Capture work and report size can increase; live scene motion,
dynamic sprites/effects, temporal history and DLSS/FG remain pending.


### 0.1.39 — material-proxy clusters and useful-density feedback

Game capture 3964 retained 97,189 pixels across seven static candidates (10.55%
of the screen), with no mask, original depth, owner or motion errors. There were
792 world records but only eight sample slots. Selection now surveys eligible
main-target world geometry before recognized HUD, instead of taking the first
eligible draws. The survey scans at most the audited renderer's 393,216 vertices
and 2,048 records, retaining a full-position fingerprint inventory, including
out-of-view eligible records. Incomplete inventories fail closed. Pairing requires
one matching fingerprint in each inventory **and** full selected geometry equality;
hashing can reject a collision but cannot establish object identity.

Candidates are clustered by vertex shader hash, fragment program address,
texture-0 address and logarithmic vertex-count bucket. These are runtime material
and geometry proxies; neither object type nor camera distance is identified.
Each group supplies its highest-scoring representative first. When there are more
than eight groups, the eight highest-scoring groups win; otherwise remaining slots
are filled by other high-scoring draws. Ties use ascending draw index. This keeps
diversity within the existing eight-world/twelve-total sample budget.

Score is clipped projected triangle area multiplied by `0.2 + 0.8 * useful_rate`.
Area is an estimate (overlap, culling, transparency and occlusion are not removed).
Cold groups use rate 0.5. After a valid original-shader/pre-HUD-depth capture,
useful density is retained pixels divided by isolated original-shader surviving
pixels, aggregated once per group. Existing estimates use a 0.75/0.25 exponential
average. Invalid owners, motion or depth mismatches suppress learning. Missing
masks do not provide observations. The 20% score floor permits renewed selection
after occlusion; it is not a guarantee that every group will be explored.

Up to 256 group observations persist in the preview profile's optional
`captures/scenery-feedback-v1.bin`, with version, bounded count, semantic checks
and checksum. A missing/truncated/invalid cache is ignored. Runtime addresses can
change between runs, so previous groups may not match and then use cold-start
scores. Normal launches strip the feedback environment variable and do not survey
or update history. Earlier preview captures do not prepopulate this new cache.

`motion-alpha-depth-check.json` reports selection policy, group count, inventory
completeness, history loaded/updated and per-draw score/area/prior observations.
Original draw-owned masks, verified pre-HUD depth and unknown-pixel semantics are
unchanged. The two per-draw timeline slots now follow selected representatives.
Tests add inventory/area/ranking and clustering/history suites to the fifteen
existing graphics suites. The audited SDK lock and consolidated patch stay intact.
These are capture diagnostics; complete scene visibility, blend contribution,
dynamic objects, temporal integration, DLSS and frame generation remain pending.


### 0.1.40 — classify remaining candidate-field gaps

Preview 0.1.39 game captures 2630/4120 retained 748,864/740,275 pixels (81.26%/
80.32%) with eight matched shader masks, complete 792-record inventories and no
mask/depth/owner/motion errors. The second capture used eight learned history
records. Different camera positions mean these are not a controlled comparison.

`motion-gap-check.json` partitions every render pixel by the first conservative
filter stage: retained candidate, no selected candidate, missing pre-HUD depth,
pre-HUD depth rejection, missing original mask, owning shader coverage rejection,
original depth mismatch or invalid evidence. `motion-gap-classification.bmp` uses
the RGB legend in the JSON. Reasons do not identify object type; missing shader
coverage alone does not prove alpha discard. A valid stationary vector remains
retained. Unknown pixels remain unknown. The report also counts retained pixels
owned by blend-enabled draws; blend contribution remains unvalidated even where
alpha and depth agree. This diagnostic adds no new motion acceptance and changes
neither sample selection nor normal rendering. It runs only during explicit
capture. The new classification test joins the previous 17 graphics suites.


### 0.1.41 — conservative recovery across selected static draws

Capture 1125 retained 83.00%, with 10.11% without a selected candidate and 6.89%
rejected by pre-HUD depth. Battle capture 4850 retained 84.89%, with 12.00% depth
rejection and 3.11% absent owning-shader coverage. Both loaded the history cache
and had no unavailable masks, original depth mismatch or invalid motion evidence.
All retained candidates had blending enabled; blend contribution is still pending.

The existing per-draw GPU raster diagnostics now accumulate candidates that pass
their own original shader mask/depth and the same frozen pre-HUD scene depth.
The experiment can fill a baseline hole only when the original combined owner
has a matched mask with zero surviving shader coverage there. Missing masks,
no original owner, surviving occluded front geometry and invalid evidence cannot
permit recovery. Multiple surviving selected draws must agree within 0.25 render
pixels against a fixed vector anchor; conflicts remain unknown. All selected
layer rasters/masks and pre-HUD depth must be available or recovery fails closed.
No additional GPU replay is needed; one bounded per-pixel accumulator is added
and freed after the explicit capture.

`motion-layer-recovery-check.json` reports eligibility, recovery, disagreement,
baseline and combined candidate coverage. In `motion-layer-recovery-coverage.bmp`,
green is the unchanged baseline and cyan is recovered coverage.
`motion-layer-recovery-vectors.bmp` shows the diagnostic recomposed vectors. The original
field/feedback is unchanged. This does not resolve alpha layers inside a single
draw, dynamic object motion or blend contribution and is not applied to the game
or DLSS. This is not a union of unvalidated masks: every admitted alternative
passes its own shader/depth and scene depth tests. The new suite tests rejected
front recovery, stationary vectors, occluded rear rejection, unavailable masks,
conflicting motion and baseline preservation, joining the prior 18 graphics suites.


### 0.1.42 — sprite discovery with stationary scenery references

The preview launcher adds an explicit capture goal: scenery/camera rotation or
sprites/character movement. The goal is immutable for a running capture session,
recorded in session.json and stripped from normal-launch environments. In sprite
mode, automatic capture waits for a uniquely signed small draw's attribute-0
center and geometry to change while its transform rows and at least one paired
static scenery reference remain unchanged. This is evidence from known world
transform rows, not an independently identified camera or object registry.

`sprite-discovery-check.json` surveys eligible small main-target vertex-program
draws before the recognized HUD, up to the renderer's 2,048-record bound and 96
vertices per draw. Larger batches and legacy paths are counted as excluded. Each
record carries shader/fragment/texture/topology/vertex-count signature, full
position/UV fingerprints, center and the first six current/previous positions and
UVs when a signature is unique in both consecutive inventories. World shader
99362655 also records projected clip positions using the validated transform.
Other shader projections are explicitly unavailable. Nonfinite values are never
used to arm movement capture. Duplicate signatures remain unpaired; an atlas
shared by two characters cannot establish identity. UV changes can expose sprite
animation but do not establish correspondence across animations.

This is discovery data, not sprite classification or motion acceptance. No
heuristic signature matching is applied to DLSS or normal rendering. Existing
static-layer reports are retained. Test instructions: choose sprite goal before
opening the game, keep the camera still and press F8 during character movement.
Timeout reports remain useful but may contain no eligible changed record. The
new signature discovery suite covers reordering, duplicate signatures in either
inventory, stationary/moving centers and invalid geometry. Launcher tests cover
explicit goal propagation and environment cleanup.


### 0.1.43 — observed direct sprite projection and camera hypothesis

The three 0.1.42 captures timed out with changing static-reference transforms.
Changed small geometry used VP 2003CB89. Its captured assembly reads only rows
512/513 for position: `clip.xyz = ATTR0.xyz * scale.xyz + ATTR0.w * offset.xyz`,
`clip.w = ATTR0.w`. Rows 256..259 are unused; their changes were irrelevant to this
path. Discovery now stores scale/offset and projects this path. Sprite mode arms
on a unique direct-path candidate's changed geometry, including automatic camera
following; it does not require stationary scenery references.

`sprite-projection-check.json` checks up to 128 signature-unique six-vertex
candidates in a supported full-frame viewport. Subviewports remain excluded.
An isolated compute probe applies the algebraically equivalent
identity-world transform to each current and previous point and compares its
previous-minus-current pixel delta against the CPU direct formula. This verifies
formula consistency, not object identity, vertex correspondence or the original
shader's full raster visibility. `sprite-candidate-overlay.bmp` labels candidate
screen bounding boxes with draw indices on the exact selected reference image.
These labels are local draw indices, not persistent character IDs.

A separate camera-only reprojection hypothesis is reported when at least two
unchanged-geometry world references agree on current/previous projection data.
Nonzero world bias, singular/nonfinite transforms and disagreements fail closed.
It inverse-projects current sprite clip position with the world reference and
reprojects that point in the previous reference, reporting observed screen delta,
estimated camera delta and an **unvalidated residual**. Cross-shader depth-space
compatibility and actual billboard vertex correspondence are not proven, so this
residual is never applied to live motion or DLSS. Valid live motion must retain
both camera and object movement; this separation is for discovery only.
Animated UVs remain explicit.
Normal rendering and the baseline static fields stay unchanged. New tests cover
the direct position rule, homogeneous W, reprojection direction, singular/bias
rejection and invalid values, joining the previous twenty graphics suites.
