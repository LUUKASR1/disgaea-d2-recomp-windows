# Preview 0.1.17 — HUD após o upscaling

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
O modo de apresentação rejeita HUD com stencil ativo, além dos casos já sem
suporte, e usa a imagem original ampliada nesses frames. Barras e indicadores
ligados ao mundo ainda pertencem à cena. O título e menus não reconhecidos
também seguem o caminho alternativo.

Com a opção ligada, a superfície de profundidade passa a R24G8 typeless com
DSV D24S8. Uma cópia da profundidade anterior à HUD é reamostrada na GPU por
vizinho mais próximo, por um shader que escreve SV_Depth na resolução de saída.
O stencil não é reconstruído; por isso qualquer HUD que o dependa é rejeitada.
Os próprios PSOs, blend, constantes, vértices, viewport/scissor e ordem dos
draws da interface são reutilizados. As referências de PSO ficam retidas por
frame até o fence do SDK permitir reutilização.

Os passes usam a mesma fila DirectX 12, com recursos e allocators próprios,
sem readback ou espera extra da CPU durante apresentação comum. O wait do SDK
no início do frame e seu fence ao final abrangem os passes adicionais. A captura
continua sendo pontual e faz os readbacks necessários à comparação.

DLSS, FSR temporal, motion vectors/camera history e frame generation continuam
pendentes. Esta etapa trata apresentação da HUD com upscaling espacial.
