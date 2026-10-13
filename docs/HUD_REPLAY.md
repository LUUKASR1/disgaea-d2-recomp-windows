# Preview 0.1.16: separação experimental nas capturas

Esta etapa reproduz os desenhos finais da HUD em alvos de diagnóstico no
DirectX 12. A imagem normal do jogo continua sendo a referência. O upscaling
normal ainda recebe cena e interface juntas; desenhar a HUD após o upscaling
será uma etapa posterior, condicionada à validação destes resultados.

## Uso

1. Abra **Arquivos e suporte** e habilite a captura antes de iniciar o jogo.
2. Clique em Jogar e aguarde a compilação do novo runtime separado.
3. Pressione F8 no Castle Hallway, numa batalha com o menu de ações aberto,
   durante um ataque de Laharl e durante o ataque do Mothman.
4. Feche o jogo normalmente e abra a pasta do relatório no launcher.
5. Envie o ZIP de capturas para analisar as imagens e comparações.

Até oito capturas por execução. A captura pode causar uma pausa enquanto os
readbacks e as reproduções extras são gravados. Nada disso roda continuamente
com a captura desativada. O título, menus desconhecidos, frames com alvos
offscreen e desenhos fora do padrão conhecido não são separados automaticamente.

## Arquivos por frame reconhecido

| Arquivo | Conteúdo |
| --- | --- |
| `backbuffer.bmp` | Frame original completo na resolução interna. |
| `scene.bmp` | Imagem imediatamente anterior ao bloco candidato de HUD. |
| `hud.bmp` | HUD reproduzida em alvo inicialmente transparente; o BMP mostra o fundo preto. |
| `recomposed.bmp` | Cena original com os desenhos da HUD reproduzidos novamente, na mesma resolução. |
| `hud-split.json` | Fronteira, tamanho, verificação dos vértices e diferenças de pixels. |

`exact-match` exige zero pixels RGB e alpha diferentes no readback da GPU e
imagens completas. `mismatch` preserva o resultado divergente para investigação.
`failed` indica falha de reprodução/leitura/gravação. `not-supported` indica
ausência de uma fronteira conservadora reconhecida. Não há promessa de que
todos os menus ou cenas sejam reconhecidos.

## Critérios e reprodução

O primeiro relatório indicou um bloco final com VS `2003CB89`, alvo principal,
coordenadas homogêneas w=1 e z menor que 0,99. A rotina agora verifica **todos**
os vértices de cada draw, os limites do buffer e a ausência de desenhos candidatos
intercalados anteriormente. Exige geometria de cena com VS `99362655` antes da
HUD e rejeita frames com draws não VP, clears intermediários, MRT ou alvos
offscreen. Nenhuma regra é aplicada a todos os shaders 2D indiscriminadamente.

Na fronteira, cor e depth/stencil são copiados com barriers no command list
original e restaurados ao estado anterior. O frame original continua sendo
desenhado normalmente. Cada draw da HUD retém o **PSO realmente usado**, sem
recompilar shaders de memória que o jogo possa modificar depois. A reprodução
usa a mesma ordem, vertex buffer, descriptors, constantes, viewport, scissor,
blend, depth e stencil. Cada reprodução recebe uma cópia do depth/stencil da
fronteira. A composição não depende de adivinhar a convenção de alpha dos atlas;
ela reproduz os próprios desenhos sobre a cena copiada.

Os recursos, allocators e fences de diagnóstico são independentes. Os draws
referenciados são liberados após a captura e os recursos no fechamento. As
imagens e os logs gerados não entram no repositório ou no launcher distribuído.

## Validação

CI compila o adaptador completo do SDK auditado e testa a reprodução no D3D12
WARP com blend alpha, depth/stencil preservados, frames consecutivos e detecção
de divergência deliberada. Um quarto vértice de cena precisa invalidar um draw
cuja amostra inicial de três vértices parecia HUD. A igualdade dos testes
sintéticos não substitui a comparação das capturas reais de gameplay.

Ainda não são produzidos motion vectors, camera jitter/histórico ou profundidade
linear para DLSS/FSR temporal/frame generation.

## Validação em gameplay recebida em 13/10/2026

A sessão 0.1.16 foi executada em 1280×720 interno e de saída, janela, sem
upscaler. As quatro capturas terminaram como `exact-match`. Cada uma registrou
zero pixels RGB diferentes, zero pixels alpha diferentes e erro RGB máximo
zero. Uma comparação independente dos pixels dos BMPs de referência e
recomposição confirmou igualdade RGB nas quatro imagens. O log terminou com
encerramento solicitado, savedata quiescent e código de saída 0.

| Captura | Cena inspecionada | Fronteira | Draws isolados | Resultado |
| --- | --- | ---: | ---: | --- |
| 00 | Castle Hallway com painel de personagem | 816 | 41 | Idêntico |
| 01 | Menu de ações e informações da batalha | 285 | 49 | Idêntico |
| 02 | Ataque de Laharl com indicadores de dano/crítico | 351 | 26 | Idêntico |
| 03 | Ataque do Mothman com indicador de Miss | 435 | 17 | Idêntico |

A inspeção das imagens isoladas confirmou os elementos da camada final de
interface e preservou personagens e partículas nas imagens da cena. Barras
de vida e outros indicadores ligados aos objetos do mundo ainda aparecem
na cena; a prova não representa remoção de toda interface possível do jogo.
Nenhuma captura ou imagem do usuário foi adicionada ao repositório.

Esta validação autoriza avançar tecnicamente para a experiência de HUD depois
do upscaling, mas não demonstra equivalência em outras resoluções, cobertura
de todos os menus ou integração de upscaling temporal. A próxima implementação
deve validar a composição em resolução nativa e depois em resoluções interna
e de saída diferentes, incluindo os estados de depth/stencil da HUD.
