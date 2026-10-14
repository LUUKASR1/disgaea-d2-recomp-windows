# Preview 0.1.19 — HUD após o upscaling

Em **Vídeo**, a opção **HUD após upscaling — experimental** habilita a
apresentação da camada final de interface na resolução de saída. Ela é
desligada por padrão e alterações exigem reiniciar o jogo. A preparação,
atualizações, DLCs e saves existentes são reutilizados; um runtime novo é
compilado separadamente.

## Teste inicial

1. Selecione 1280×720 interno e de saída, upscaling desligado, e habilite a HUD
   após upscaling. Salve e abra o jogo com captura habilitada.
2. Capture Castle Hallway e o menu da batalha com F8. Feche o jogo e envie o ZIP.
3. Depois, teste saída 1920×1080, interno 1280×720, FSR 1 e a mesma opção ligada.
   Capture menus e golpes de Laharl e do Mothman e envie o segundo relatório.

`output.bmp` mostra a imagem efetivamente apresentada, já na resolução de
saída. `presentation.json` indica se o frame usou HUD separada ou o caminho
alternativo. Em resolução nativa, com o upscaler desligado, a captura compara
o resultado com o frame original completo. Em resoluções diferentes, os
pixels mudam intencionalmente; a comparação visual é necessária e não se
declara igualdade pixel a pixel.

## Pipeline

O renderer preserva um frame original completo para referência e alternativa.
Nas cenas reconhecidas, copia cor e depth antes do bloco final de HUD, amplia
a **cena** por FSR 1 ou bilinear, e reproduz os draws reais da interface sobre
o backbuffer na resolução de saída. O buffer interno ainda desenha a HUD de
referência; esta primeira implementação favorece validação e reversibilidade,
não elimina esse trabalho duplicado nem promete aumento de FPS.

A regra conservadora e a verificação de todos os vértices de 0.1.16 permanecem.
O modo de apresentação reconstrói o stencil nas cenas reconhecidas e usa a
imagem original ampliada nos demais casos sem suporte. Barras e indicadores
ligados ao mundo ainda pertencem à cena. O título e menus não reconhecidos
também seguem o caminho alternativo.

Com a opção ligada, a superfície de profundidade passa a R24G8 typeless com
DSV D24S8. Uma cópia da profundidade anterior à HUD é reamostrada na GPU por
vizinho mais próximo, por um shader que escreve SV_Depth na resolução de saída.
O stencil é lido pelo SRV X24_TYPELESS_G8_UINT e reconstruído por oito passes
na GPU, um por bit, usando a mesma amostragem por vizinho mais próximo do depth.
Cada passe descarta pixels cujo bit é zero e escreve apenas o bit correspondente
por stencil REPLACE. Esses passes ocorrem apenas quando algum draw final de HUD
tem stencil habilitado. Os draws originais mantêm compare/op/read/write masks.
Os próprios PSOs, blend, constantes, vértices, viewport/scissor e ordem dos
draws da interface são reutilizados. As referências de PSO ficam retidas por
frame até o fence do SDK permitir reutilização.

Os passes usam a mesma fila DirectX 12, com recursos e allocators próprios,
sem readback ou espera extra da CPU durante apresentação comum. O wait do SDK
no início do frame e seu fence ao final abrangem os passes adicionais. A captura
continua sendo pontual e faz os readbacks necessários à comparação.

DLSS, FSR temporal, motion vectors/camera history e frame generation continuam
pendentes. Esta etapa trata apresentação da HUD com upscaling espacial.

## Diagnóstico de fallback (0.1.18)

Cada captura registra `hud_after_upscale_requested`, `fallback_reason` e o
upscaler selecionado em `presentation.json`. O log registra o motivo apenas
quando ele muda, além da mensagem pontual de captura; não há trace por draw.
A renderização e as regras de aceitação permanecem iguais às de 0.1.17.

Motivos: `option-disabled`, `draw-count`, `vertex-pipeline-unavailable`,
`alternate-render-path`, `unsupported-scene-draw`, `no-final-hud-block`,
`interleaved-hud`, `world-shader-not-recognized`, `hud-stencil-enabled`,
`hud-stencil-state`, `capture-prepare-failed`, `resource-prepare-failed`,
`snapshot-not-reached` ou `snapshot-failed`. HUD separada usa motivo `none`
no JSON e `separate` no log.

## Validação recebida do usuário

Em 13/10/2026, um JSON colado pelo usuário confirmou apresentação separada
em 1280×720 com comparação nativa disponível e diferenças RGB/alpha iguais
a zero. Outro confirmou apresentação separada de 1280×720 para 1920×1080.
Durante o golpe de Laharl houve alternância com fallback, sem defeitos visuais
percebidos pelo usuário. Esses dados validam os frames relatados; não constituem
inspeção independente das imagens nem identificação do motivo daquele fallback.

## 0.1.19 — stencil da HUD

A captura do golpe de Laharl enviada pelo usuário em 13/10/2026 identificou
`hud-stencil-enabled` com a opção ligada e FSR 1 selecionado. Essa versão
reconstrói os oito bits do stencil anterior à HUD na resolução de saída.
Não promete aumento de FPS: os oito passes extras são usados só nos frames
que precisam de stencil. A validação no jogo real continua pendente.

Os testes GPU exercitam stencil 0x00, 0xA5 e 0xFF, com regiões de valor oposto,
comparação EQUAL, mudança entre frames e resultados nativos/bilinear/FSR 1.
