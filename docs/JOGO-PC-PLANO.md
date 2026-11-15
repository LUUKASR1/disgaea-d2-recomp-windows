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
4. **Execução independente do launcher:** o `.exe` encontra o ISO/dados por arquivo de configuração, tela de
   primeira execução para escolher o ISO, saves em pasta do usuário, mensagens de erro claras.
5. **Acabamento de PC:** pausa/volume ao perder foco, captura de tela, controle conectado/desconectado, ícone e
   título da janela, múltiplos monitores, DPI.
6. **Empacotamento:** instalador e atualização; reempacotar o launcher com o seletor Vulkan.

## Fora de alcance (por enquanto)

Trocar DirectX 12 ↔ Vulkan com o jogo aberto (a API é escolhida antes de abrir), DLSS e geração de quadros.
