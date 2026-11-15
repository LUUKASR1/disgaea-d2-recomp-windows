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

Trocar DirectX 12 ↔ Vulkan com o jogo aberto (a API é escolhida antes de abrir), DLSS e geração de quadros.

## Lista de desejos (pedidos do usuário) e viabilidade

| Pedido | Viabilidade | Observação |
| --- | --- | --- |
| Suavização pós-processamento (FXAA/SMAA) | Alta | O passe final de pós-processamento já existe; falta o filtro. |
| Upscaler e modos (FSR) | Alta para FSR 1 | FSR 2/3 e DLSS precisam de vetores de movimento/profundidade do jogo, que não existem aqui; DLSS exige SDK da NVIDIA. |
| Estilo de ampliação / filtro de textura | Média | Anisotrópico/bilinear/nearest por sampler são simples; "estilos" (pixel art etc.) são shaders. |
| Ampliação de textura, ícones de itens, texto e personagens em HD | Média | Via **pacote de texturas**: substituir texturas por hash (como RPCS3/Dolphin). Os PNG em alta vêm de fora (feitos pelo usuário ou pela comunidade). Texto/ícones do jogo são texturas, então entram no mesmo sistema. |
| Ampliação por IA | Média, offline | Rodar um modelo (ex.: ESRGAN) em tempo real é pesado; o caminho realista é uma ferramenta que gera o pacote de texturas antes de jogar. |
| Contorno nos personagens | Baixa/Média | Um contorno por detecção de bordas na imagem toda é fácil; só nos personagens exige separar os desenhos deles. |
| HDR | Média | Swapchain HDR10 + tone mapping; depende do monitor/Windows. |
| Vídeos em HD | Baixa | Os vídeos são os do disco; só melhora trocando por arquivos em maior qualidade. |
| Jogar com mouse e teclado | Média | Teclado já funciona como controle (fixo). Falta remapear teclas na interface; mouse como "mão" do jogo exige o jogo aceitar ponteiro. |
| Slots de saves e load | Média | Gerenciador de saves (backup, importar/exportar, vários perfis) em cima das pastas de save atuais. |
