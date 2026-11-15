# Plano: Disgaea D2 como um jogo de PC (estilo Steam)

Objetivo: depois de preparado, o jogo abre pelo `.exe`, tem menu de opções **dentro do jogo**, salva as escolhas
e não depende do launcher para jogar. O launcher continua só na preparação inicial (o código do jogo é gerado
localmente a partir do ISO do próprio usuário e não pode ser distribuído pronto).

## Etapas

1. **Base de janela (feito, aguarda teste):** configurações salvas em `%LOCALAPPDATA%\DisgaeaD2Recomp\settings.ini`
   (`fullscreen`, `vsync`, `width`, `height`); tela cheia sem bordas por `F11` / `Alt+Enter`; `Esc` não fecha mais o
   jogo (use Alt+F4 ou o botão da janela); proporção mantida com barras pretas; cursor escondido em tela cheia.
   Variáveis do launcher (`D2_VSYNC`, `D2_OUTPUT_WIDTH/HEIGHT`) continuam valendo sobre o arquivo.
2. **Painel de opções dentro do jogo:** abre com `F1` (e botão do controle). Texto desenhado pelo backend com uma
   fonte embutida. Abas: Vídeo (modo de janela, resolução, V-Sync, limite de FPS, contador de FPS), Gráficos
   (escala de render, nitidez), Sobre/Atalhos. Navegação por teclado, mouse e controle. Aplica na hora.
3. **Imagem intermediária + escala de resolução + FSR 1:** o Vulkan hoje desenha direto na janela. Criar o alvo
   intermediário (resolução interna) e o passe de upscale/nitidez; depois HUD em resolução de saída.
4. **Execução independente do launcher:** o `.exe` encontra o ISO/dados por arquivo de configuração, tela de
   primeira execução para escolher o ISO, saves em pasta do usuário, mensagens de erro claras.
5. **Acabamento de PC:** pausa/volume ao perder foco, captura de tela, controle conectado/desconectado, ícone e
   título da janela, múltiplos monitores, DPI.
6. **Empacotamento:** instalador e atualização; reempacotar o launcher com o seletor Vulkan.

## Fora de alcance (por enquanto)

Trocar DirectX 12 ↔ Vulkan com o jogo aberto (a API é escolhida antes de abrir), DLSS e geração de quadros.
