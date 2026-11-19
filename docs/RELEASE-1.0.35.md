# Disgaea D2 Windows 1.0.35 — Prévia da versão europeia (BLES01939)

> **Pré-lançamento experimental.** A versão estável continua sendo a [1.0.2](RELEASE-1.0.2.md). Esta prévia existe para
> testar o jogo europeu; pode falhar na preparação ou no jogo.

## Português

### O que há de novo

- **Perfil europeu (BLES01939)** na página **Arquivos e suporte → Versão europeia**: escolha a **pasta do jogo (PS3_GAME) ou a ISO europeia** e o
  **EBOOT.elf descriptografado** do seu disco; o launcher compila o jogo no seu PC (nada do jogo é distribuído).
- Perfis **americano (1.40)** e **europeu** ficam salvos separadamente; troque com **Usar perfil europeu / americano**.
- O executável europeu aceito tem SHA256 `0257bc3ab36a63f55b0a8722fa54f7d5278a568941bdff7817934a891fcb0cbb`
  (disco e atualização europeia têm o mesmo executável).

### Novidades da 1.0.35

- **Android: o jogo começa o quadro seguinte enquanto o anterior é desenhado.** Até aqui o jogo ficava parado no flip até o quadro inteiro ser desenhado e apresentado (inclusive a espera do vsync). Agora ele é liberado assim que os vértices do quadro foram copiados, e o desenho e a apresentação correm em paralelo com o início do quadro seguinte. No mapa pesado isso deve tirar ~4 a 7 ms do período (de ~34 ms para perto de 30 ms), o suficiente para os 30 fps estáveis. `D2_EARLY_FLIP_OFF=1` volta ao comportamento anterior; o log mostra `[vulkan] early flip retire: on`. Gere o núcleo Android de novo pelo launcher.
- **Resolução interna refeita** (corrige a tela cinza abaixo de 100 %): o app reduz o buffer da superfície e o Android amplia sozinho. Precisa do APK desta versão.

- **Android: resolução interna e contador de FPS.** No painel inicial do app há agora dois botões: **Resolução interna** (50 %, 60 %, 75 % ou 100 % da tela do aparelho) e **Contador de FPS** (canto superior esquerdo, verde a partir de 28 fps). As opções valem na próxima vez que você tocar em **Iniciar núcleo do jogo** e ficam salvas. Reduzir a resolução diminui o trabalho da GPU, mas no mapa pesado o limite hoje é a CPU; o teste serve para confirmar isso. O log registra `[vulkan] android settings` e `[vulkan] rendering at WxH`. Precisa do APK novo (os botões) e do núcleo gerado de novo pelo launcher (a leitura das opções e o contador).

### Novidades da 1.0.33

- Renderizador Vulkan: a leitura de vértices do jogo agora roda em paralelo (3 threads auxiliares + a principal) enquanto o jogo espera o flip, aproveitando os núcleos parados. O resultado visual deve ser idêntico. `D2_VULKAN_FETCH_THREADS=0` volta à leitura numa thread só. Vale para Android e para o Vulkan do Windows. Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.32

- Renderizador Vulkan: formato de vértice compacto. Em vez de 16 atributos (256 B) por vértice, cada desenho grava só os atributos que o shader de vértice lê (tipicamente 3 a 4, ~48–64 B), com a leitura dos dados do jogo adiantada na memória (prefetch). Reduz o tempo de montagem dos vértices e o envio de geometria à GPU no mapa pesado. Vale para Android e para o Vulkan do Windows; o resultado visual deve ser idêntico. Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.31

- Renderizador Vulkan: cada vértice ocupa 256 B, mas o shader só lê alguns atributos. A leitura de vértices agora grava só os atributos que o shader de vértice usa (sem zerar nem copiar os 256 B), o que reduz o tráfego de memória por quadro. Vale para Android e para o Vulkan do Windows; o resultado visual deve ser idêntico. Para comparar: `D2_VULKAN_ALL_SLOTS=1` volta a gravar todos os atributos e `D2_VULKAN_SLOW_FETCH=1` volta à leitura genérica. Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.30

- Renderizador Vulkan: a leitura de vértices do jogo (≈5,3 ms por quadro no mapa pesado) agora resolve endereço e formato de cada atributo uma vez por desenho, em vez de uma vez por vértice. Vale para Android e para o Vulkan do Windows; o resultado visual deve ser idêntico (para comparar, `D2_VULKAN_SLOW_FETCH=1` volta ao caminho antigo). Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.29

- Núcleo Android: o gravador de desenhos e a leitura de vértices do Vulkan agora são compilados com `-O2` (antes `-O1`, como o resto do C++), e o processo fica restrito aos núcleos rápidos da CPU (os de 2,0 GHz deixam de ser usados). No mapa pesado o jogo e o render somavam ~37 ms por quadro, logo acima de 2 vblanks; reduzir o tempo do render deve segurar 30 fps estáveis. Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.28

- Núcleo Android: a cada 5 s o log mostra as threads mais ocupadas do processo (% de um núcleo e em qual núcleo rodaram) e a frequência atual de cada núcleo da CPU, para achar o gargalo do mapa pesado. Só diagnóstico. Gere o núcleo Android de novo pelo launcher.

- Núcleo Android: o log de desempenho passou a mostrar o tempo de CPU da thread de renderização e do processo por quadro (para separar tempo ocupado de tempo esperando o vsync) e a composição dos desenhos a cada 600 quadros. Só diagnóstico. Gere o núcleo Android de novo pelo launcher.

- Renderizador Vulkan: desenhos indexados reaproveitam vértices repetidos (cada índice distinto é buscado uma vez só) e a varredura de vértices finitos, que era redundante, só roda sob demanda. O log passa a mostrar a composição dos desenhos (tipo de primitiva, índices x vértices distintos). Gere o núcleo Android de novo pelo launcher.

- Renderizador Vulkan (Android): os desenhos passam a usar lista de índices; um quad ocupa 4 vértices em vez de 6 e nenhum vértice é copiado duas vezes. Reduz o custo por quadro nas cenas com muitos sprites (mapa pesado). Gere o núcleo Android de novo pelo launcher.

- Núcleo Android: ao minimizar e voltar para o jogo a tela não fica mais preta. O jogo pausa enquanto o app está em segundo plano e o Vulkan recria a superfície e o swapchain na volta (antes a falha da superfície encerrava o jogo). Gere o núcleo Android de novo pelo launcher.

- Núcleo Android: áudio AAudio em modo callback com buffer circular e relatório de saúde a cada 5 s (o som voltou a sair com volume normal).

- Áudio da versão europeia: corrigida uma corrida na inicialização do mixer do jogo (o jogo registrava a fila de eventos de áudio com chave 0 e ficava sem som). Vale também para o Windows. Gere o núcleo Android de novo pelo launcher.

- Renderizador Vulkan: o buffer de geometria agora é reaproveitado entre quadros e a verificação dos vértices ficou bem mais rápida, o que reduz o custo por quadro em cenas com muita geometria (mais notável no Android). Gere o núcleo Android de novo pelo launcher.

- Núcleo Android: o cache de pipelines gráficos agora fica salvo em disco, o que reduz as travadas de meio segundo ao entrar em cenas novas a partir da segunda vez. Gere o núcleo de novo pelo launcher.

- Núcleo Android: os botões e os analógicos do aparelho chegam ao jogo (cellPad). Gere o núcleo de novo pelo launcher.

- Núcleo Android: o buffer da tela do jogo passa a ter o tamanho real da tela deitada; antes a imagem ficava achatada numa faixa.

### Limites conhecidos

- Usa o layout do executável 1.00. Se algum ajuste do projeto (cheats, etc.) não casar com o código europeu, a
  compilação avisa e para; mande o log (`Abrir pasta local do aplicativo` → `preparacao.log`).
- Aceita a pasta do jogo ou a ISO do disco; dados da atualização europeia (PKG) ainda não são importados.
- A primeira preparação demora bastante (compila o jogo).

## English

**Experimental pre-release.** Stable remains [1.0.2](RELEASE-1.0.2.md).

- **European profile (BLES01939)** under **Files and support → European version**: pick the European game folder (PS3_GAME) or ISO and the
  decrypted EBOOT.elf from your disc; the launcher compiles the game on your PC (no game content is distributed).
- US 1.40 and European profiles are saved separately; switch with **Use European / US profile**.
- Accepted European executable SHA256: `0257bc3ab36a63f55b0a8722fa54f7d5278a568941bdff7817934a891fcb0cbb`.
- Known limits: uses the 1.00 executable layout (the build stops with a log if a project hook does not match);
  the disc game folder or ISO is accepted, European update (PKG) data is not imported yet; the first preparation is slow.
