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
