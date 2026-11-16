# Plano: Disgaea D2 como um jogo de PC (estilo Steam)

Objetivo: depois de preparado, o jogo abre pelo `.exe`, tem menu de opções **dentro do jogo**, salva as escolhas
e não depende do launcher para jogar. O launcher continua só na preparação inicial (o código do jogo é gerado
localmente a partir do ISO do próprio usuário e não pode ser distribuído pronto).

## Etapas

1. **Base de janela (feito e testado):** configurações salvas em `%LOCALAPPDATA%\DisgaeaD2Recomp\settings.ini`
   (`fullscreen`, `vsync`, `width`, `height`); tela cheia sem bordas por `F11` / `Alt+Enter`; `Esc` não fecha mais o
   jogo (use Alt+F4 ou o botão da janela); proporção mantida com barras pretas; cursor escondido em tela cheia.
   Variáveis do launcher (`D2_VSYNC`, `D2_OUTPUT_WIDTH/HEIGHT`) continuam valendo sobre o arquivo.
2. **Painel de opções dentro do jogo (feito, testado):** `F1` abre um painel com tema Disgaea, desenhado pelo
   backend com uma fonte embutida (`tools/gen_ui_font.py` gera `port/vulkan/ui_font.inc`). Controlado pelo mouse
   (o jogo já lê o teclado, então navegar por setas moveria o personagem). Aba VÍDEO: modo de tela, tamanho da
   janela, resolução interna, V-Sync, contador de FPS. Aba FILTROS: nitidez, scanlines, máscara CRT, vinheta,
   cores vivas. Tudo salvo em `settings.ini`. Pendente: navegação por teclado/controle com o jogo pausado (exige
   mexer no SDK de entrada).
3. **Imagem intermediária + resolução interna + filtros (feito, testado):** com escala ≠ 100% ou algum filtro
   ligado, o jogo é desenhado numa imagem própria (50–200% da janela, no máximo 7680 px) e um passe final aplica
   os filtros e ajusta para a janela (sem filtro/escala, o caminho continua direto). Pendente: FSR 1 e HUD
   desenhado em resolução de saída (hoje o painel passa pelos filtros).
4. **Execução independente do launcher (em teste):** depois que o launcher (ou `Rodar-Jogo-Vulkan.bat`) roda o
   jogo uma vez, ele grava `DisgaeaD2.ini` ao lado do `.exe` (caminhos do ELF e do ISO/dados, pasta de trabalho e as
   variáveis de ambiente usadas). Aberto **sem argumentos** (duplo clique ou atalho), o `.exe` lê esse arquivo, não
   mostra console (log em `%LOCALAPPDATA%\DisgaeaD2Recomp\logs`, só os 20 mais recentes) e já usa as opções
   salvas dentro do jogo. `DisgaeaD2Recomp.exe --create-shortcut` (ou `Criar-Atalho-Jogo.bat`) cria o atalho na Área
   de Trabalho. Sem o `.ini`, mostra uma mensagem pedindo para preparar pelo launcher. Limite: a preparação (gerar o
   código a partir do ISO do próprio usuário, instalar update/DLC) continua no launcher.
5. **Acabamento de PC:** pausa/volume ao perder foco, captura de tela, controle conectado/desconectado, ícone e
   título da janela, múltiplos monitores, DPI.
6. **Empacotamento:** instalador e atualização; reempacotar o launcher com o seletor Vulkan.

## Fora de alcance (por enquanto)

Trocar DirectX 12 ↔ Vulkan sem reiniciar o jogo (ver lista de desejos: troca com reinício automático), DLSS e geração de quadros.

## Lista de desejos (pedidos do usuário) e viabilidade

| Pedido | Viabilidade | Observação |
| --- | --- | --- |
| Suavização pós-processamento (FXAA/SMAA) | Alta | O passe final de pós-processamento já existe; falta o filtro. |
| Upscaler e modos (FSR) | Alta para FSR 1 | FSR 2/3 e DLSS precisam de vetores de movimento/profundidade do jogo, que não existem aqui; DLSS exige SDK da NVIDIA. |
| Estilo de ampliação / filtro de textura | Média | Anisotrópico/bilinear/nearest por sampler são simples; "estilos" (pixel art etc.) são shaders. |
| Ampliação de textura, ícones de itens, texto e personagens em HD | Média | Via **pacote de texturas**: substituir texturas por hash (como RPCS3/Dolphin). Os PNG em alta vêm de fora (feitos pelo usuário ou pela comunidade). Texto/ícones do jogo são texturas, então entram no mesmo sistema. |
| Ampliação por IA | Média, offline | Rodar um modelo (ex.: ESRGAN) em tempo real é pesado; o caminho realista é uma ferramenta que gera o pacote de texturas antes de jogar. |
| HDR | F1 > Filtros > "HDR" (Desligado/Leve/Médio/Forte), DX12. Swap chain scRGB FP16 (D2_HDR=1, reinicia ao ligar/desligar); passe final expande os brilhos mantendo o matiz, branco SDR segue o ajuste do Windows (ou D2_HDR_PAPER_NITS). Sem HDR no Windows a imagem fica igual ao SDR. Screenshots saem em SDR. Vulkan: pendente. |
| Contorno nos personagens | F1 > Filtros > "Contorno nos personagens" (Desligado/Leve/Médio/Forte), DX12, **confirmado pelo usuário**. Sprites indexados (B8 + paleta 256x1, 3–96 vértices, shader 2003CB89) são redesenhados 8x deslocados com a silhueta alfa em preto. Com a opção ligada o log traz tabela `[outline] tex`. Overrides: `D2_OUTLINE_VS`, `D2_OUTLINE_TEX0`. Vulkan: pendente. |
| Vídeos em HD | Baixa | Os vídeos são os do disco; só melhora trocando por arquivos em maior qualidade. |
| Jogar com mouse e teclado | Média | Teclado já funciona como controle e as teclas são remapeáveis na aba CONTROLES do F1 (feito, testado). Mouse como "mão" dentro do jogo exige o jogo aceitar ponteiro (estruturas do jogo ainda não mapeadas). |
| Slots de saves e load | Feito | Gerenciador de backups no launcher e diálogo de slots dentro do jogo (salvar/carregar). |
| Pacote de texturas (HD) | Feito (DirectX 12 confirmado; Vulkan em teste) | Dump: aba SISTEMA do F1 > "Salvar texturas (PNG)" (ou `D2_TEXTURE_DUMP=1`, ou `texdump=1`) grava cada textura em `%LOCALAPPDATA%\DisgaeaD2Recomp\texture-pack\dump\<hash>_<L>x<A>_f<fmt>.png`. Troca: PNG em `texture-pack\` cujo nome começa com o hash substitui a textura (qualquer tamanho). Funciona em DirectX 12 e Vulkan. `Ampliar-Texturas.bat` (tools/d2_texture_upscale.py) amplia o dump 2x/3x/4x para o pacote (Lanczos com Pillow, ou Real-ESRGAN com `--realesrgan`). Sprites de paleta (cinza) não devem ser trocados. |
| Seletor de save no jogo (DirectX 12) | Feito | Mesma lista de slots e confirmações do Vulkan, em janela GDI sobre o jogo; mouse (clique, botão direito, roda) e teclado/controle. |
| Captura de tela (tecla configurável) | Feito (em teste) | Tecla padrão F12, regravável em F1 > CONTROLES ("Captura de tela"; Print Screen também serve). Grava PNG sem perdas na resolução de saída, já com upscaler e filtros, em `Imagens\Disgaea D2` (`D2_SCREENSHOT_DIR` muda a pasta). No Vulkan o quadro da captura sai sem menu/FPS. Aviso na tela por 3,5 s. |
| Trocar a API gráfica (D3D12 ↔ Vulkan) pelo overlay dentro do jogo | Feito (em teste) | Aba SISTEMA do F1 (Vulkan): clique duas vezes na linha "API gráfica" grava `api=0` em `settings.ini` e reinicia o jogo em DirectX 12 pelo caminho normal de fechamento (espera um save em andamento). Na janela do DirectX 12, F1 pergunta se quer reiniciar em Vulkan (`api=1`). A escolha salva vale sobre a do launcher; o progresso não salvo é perdido. O Idioma foi para essa aba. |
| Multithreading x86/x64 | Média/Baixa | Investigar: threads separadas para o processamento de comandos do RSX/pós-processamento e para o áudio, e opções de quantidade de threads no overlay. O alvo continua sendo 64 bits (x64); 32 bits (x86) não está previsto, pois o recomp e os backends gráficos são 64 bits. |
| Código compilado direto | Alta (já é o modelo) | O jogo já é recompilação estática: o PPU é traduzido para C/C++ e compilado, sem interpretador. Dá para expor no overlay o estado e opções de build (por exemplo perfis de otimização, LTO/PGO e SIMD) e acelerar mais as funções críticas. |
| Painel F1 no DirectX 12 | Feito (em teste) | O backend D3D12 do SDK não desenha 2D, então o painel é uma janela própria (GDI, semitransparente) sobre o jogo, em `port/d2_d3d12_options.inc`. Abas VÍDEO (tela cheia/janela, tamanho da janela, resolução de saída, resolução interna, upscaler AMD FSR 1.0, V-Sync, FPS), FILTROS (nitidez, scanlines, máscara CRT, vinheta, cores vivas, FXAA), CONTROLES (remapear teclas) e SISTEMA (idioma, API gráfica → Vulkan). Saída, resolução interna, upscaler e V-Sync valem ao reiniciar (botão "Reiniciar agora"): o pipeline `port/graphics` já os lia por `D2_OUTPUT_*`, `D2_RENDER_*` e `D2_UPSCALER`, e o painel só os grava em `settings.ini`. Os filtros são um passe de compute novo em `port/graphics/graphics.cpp` (liga com `D2_POST_FILTERS=1`, ligado por padrão). F11/Alt+Enter alternam a tela cheia (16:9 com barras pretas) e o Esc deixa de fechar o jogo. Usa o mesmo `settings.ini`. |


## Pendências para a versão 1.0 (acordado: fazer por último)
- Contorno nos personagens no Vulkan.
- Contorno: HUD em telas sem bloco de HUD detectável pode ser contornado.
- Real-ESRGAN (ampliador de texturas) ainda não testado.
- Mouse como ponteiro dentro do jogo (estacionado).
- Etapas 5/6: pausar ao perder foco, aviso de controle conectado/desconectado, ícone, multi-monitor/DPI, instalador.
