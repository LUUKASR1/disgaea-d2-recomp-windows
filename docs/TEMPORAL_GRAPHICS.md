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
