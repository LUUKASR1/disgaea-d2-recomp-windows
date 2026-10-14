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
