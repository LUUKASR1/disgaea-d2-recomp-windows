# Disgaea D2 Windows 1.0.44 — Prévia da versão europeia (BLES01939)

> **Pré-lançamento experimental.** A versão estável continua sendo a [1.0.2](RELEASE-1.0.2.md). Esta prévia existe para
> testar o jogo europeu; pode falhar na preparação ou no jogo.

## Português

### O que há de novo

- **Perfil europeu (BLES01939)** na página **Arquivos e suporte → Versão europeia**: escolha a **pasta do jogo (PS3_GAME) ou a ISO europeia** e o
  **EBOOT.elf descriptografado** do seu disco; o launcher compila o jogo no seu PC (nada do jogo é distribuído).
- Perfis **americano (1.40)** e **europeu** ficam salvos separadamente; troque com **Usar perfil europeu / americano**.
- O executável europeu aceito tem SHA256 `0257bc3ab36a63f55b0a8722fa54f7d5278a568941bdff7817934a891fcb0cbb`
  (disco e atualização europeia têm o mesmo executável).

### Novidades da 1.0.44

- **Android: menu dentro do jogo.** Durante o jogo, o botão **Voltar** (ou apertar **L3 + R3**, os dois analógicos, no portátil) abre o menu: **Voltar ao jogo**, **Resolução interna**, **Contador de FPS**, **Liberação antecipada**, **Painel de diagnóstico** e **Sair do jogo**. O contador de FPS e a liberação antecipada passam a valer na hora (o núcleo relê o arquivo de opções a cada ~1,5 s; núcleo desta versão). **Sair do jogo** pede confirmação, bloqueia novos salvamentos, espera o salvamento em andamento terminar e encerra o app, como na versão de Windows. Com um núcleo mais antigo o app também fecha, só sem a espera pelo salvamento.
- **Ícone próprio do app Android** (desenho original, ícone adaptativo e monocromático para Android 13+).
- Gere o núcleo Android de novo pelo launcher e instale o APK desta versão.

### Novidades da 1.0.43

- **Android: perfilador mais informativo (diagnóstico).** No teste da 1.0.42 o caminho rápido de memória reduziu pouco o custo da thread do jogo (~61 % para ~57 % de um núcleo), e cerca de um quarto do tempo da thread cai em endereços fora do jogo que o perfilador não nomeava. Agora o log nomeia a função da biblioteca (ou o mapeamento de memória) e mostra de onde ela foi chamada (`called from`). Sem mudança no jogo. Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.42

- **Android: correção da 1.0.41.** O caminho rápido de leitura/escrita de memória do jogo descrito abaixo **não estava incluído na 1.0.41** (a alteração ficou fora do pacote). Agora ele é aplicado durante a geração do núcleo Android, numa cópia do SDK, sem mudar o SDK fixado e **sem precisar preparar o jogo de novo**. Ao gerar o núcleo, o log mostra `Ajustes Android do SDK (acesso rapido a memoria do jogo): aplicado`. Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.41

- **Android: acessos à memória do jogo mais baratos.** O perfilador da 1.0.40 mostrou que a thread do jogo gasta cerca de um terço do tempo nas funções `vm_read32`, `vm_write32` e `vm_write64`, por causa das verificações de diagnóstico feitas em cada acesso. Quando nenhuma variável de diagnóstico está definida, esses acessos agora seguem um caminho curto, com o mesmo comportamento (limites, página nula, registradores SPU, detecção de espera ativa, coerência). `PPU_VM_SLOW=1` volta ao caminho antigo. O Windows não muda. Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.40

- **Android: perfilador de amostragem (diagnóstico).** A cada 30 s o log traz `[profile]` com as regiões de código mais executadas nas duas threads mais ocupadas (jogo e render). Serve para descobrir o que consome os ~20 ms por quadro da thread do jogo no mapa pesado. Sem mudança visual; custo desprezível. `D2_PROFILE_OFF=1` desliga. Gere o núcleo Android de novo pelo launcher.

### Novidades da 1.0.39

- **Android: código do jogo compilado com -O2.** Nos testes, a thread do jogo (~22 ms por quadro no mapa pesado) é o limite dos 60 fps. O código traduzido do PS3 era compilado com -O1 para manter a compilação rápida; agora usa -O2. **A geração do núcleo no launcher ("Preparar núcleo Android") fica mais demorada** e usa mais memória. Gere o núcleo de novo; o APK da 1.0.36 continua servindo. Para voltar ao -O1: `build_android_core.py --ppu-opt 1`.

### Novidades da 1.0.38

- **Vulkan (Android e Windows): o formato de vértice compacto agora é realmente compacto.** O diagnóstico da 1.0.37 mostrou que cada vértice ainda ocupava 16 slots (256 B): os vertex shaders do jogo eram gerados declarando sempre os 16 atributos de entrada, então a "versão compacta" das 1.0.31–1.0.35 não reduzia nada. Agora o shader declara só os atributos que o programa lê (análise estática do decompilador; qualquer dúvida mantém os 16), tipicamente 3 a 4 slots por vértice. Isso reduz a escrita dos vértices no registro do quadro e o envio de geometria à GPU (~22 MB por quadro no mapa pesado). Se o shader compacto não compilar, aquele programa volta aos 16 atributos. O log mostra `vertex inputs: narrowed|all 16` por programa. `D2_VS_FULL_INPUTS=1` volta ao comportamento anterior. O resultado visual deve ser idêntico. Gere o núcleo Android de novo pelo launcher; o APK da 1.0.36 continua servindo. Na primeira execução os shaders são recompilados (uma pequena pausa) e depois ficam em cache.

### Novidades da 1.0.37

- **Diagnóstico de pacing (Android e Windows/Vulkan):** o log passa a trazer, a cada 600 quadros, `[vulkan][pace] host waits per frame` com o tempo médio que a thread de render ficou bloqueada em cada espera (fence do upload de geometria, fence do desenho, aquisição da imagem da swapchain e apresentação). Sem mudança de comportamento. Serve para descobrir o que prende o período do mapa pesado em ~34 ms. Gere o núcleo Android de novo pelo launcher; o APK da 1.0.36 continua servindo.

### Novidades da 1.0.36

- **Android: botão "Liberação antecipada" no painel do app** (ligada por padrão). Ela liga/desliga a mudança da 1.0.35 (liberar o jogo logo após a cópia dos vértices, sobrepondo desenho e apresentação ao quadro seguinte), para comparar o desempenho e a imagem, e para isolar qualquer diferença visual (por exemplo, nas texturas do chão). O log registra `early flip retire: on|off` e a linha `android settings`. Precisa do APK e do núcleo desta versão (gere o núcleo de novo pelo launcher).

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
