# Captura pontual para investigar a separação da HUD

A preview 0.1.15 adiciona controle por evento Win32 ao capturador já existente
no SDK auditado. Nenhum filtro de HUD, DLSS ou frame generation é ativado.
A imagem `backbuffer.bmp` é a cena na resolução interna **ainda com a HUD**,
antes do estágio de upscaling. Não é uma captura da imagem final do monitor.

## Uso

1. Abra **Arquivos e suporte** e marque **Habilitar captura na próxima abertura do jogo**.
2. Clique em Jogar. A primeira execução compila um runtime novo, separado.
3. Pressione **F8** com a janela do jogo em primeiro plano, ou use o botão
   **Capturar frame** no launcher. Aguarde a confirmação antes de pedir outra captura.
4. Capture o título, Castle Hallway, uma batalha com o menu de ações aberto
   e, se possível, um efeito de ataque. O limite é oito capturas por execução.
5. Feche o jogo normalmente. O launcher gera `DisgaeaD2-Capturas-*.zip`
   em `Downloads` do perfil do Windows, se essa pasta existir. Caso contrário,
   usa `captures/reports` na pasta local do aplicativo. O botão **Abrir pasta do
   relatório** abre o local efetivamente usado após o fechamento.

Sem a opção marcada, o launcher remove variáveis herdadas de captura e o
renderizador não faz readbacks de diagnóstico. Com a opção marcada, ele
registra os campos necessários em cada draw e verifica um evento em memória
por frame apresentado. Escrita de manifestos e cópias de imagens ocorrem apenas
na solicitação; a espera pela GPU pode causar uma pausa durante essa captura.
F8 só dispara com a janela do jogo em primeiro plano. Um evento auto-reset
isolado por UUID evita repetir o pedido em todos os frames. O handle é fechado
ao sair do jogo. Cada sessão tem uma pasta própria e não sobrescreve capturas.

## Conteúdo

- `session.json`: versão, configurações e limite da sessão.
- `frame_XX/manifest.txt`: draws/clears em ordem da lista reproduzida pelo SDK,
  alvo de renderização, viewport, endereços de shaders, hash do VS registrado
  no draw, texturas, blend, depth/stencil, scissor e amostras existentes de constantes.
  Endereços e slots de cache não são identidades estáveis entre execuções.
- `frame_XX/backbuffer.bmp` e `rt_*.bmp`: cena e alvos intermediários usados.
- `frame_XX/complete.json`: conclusão ou falha das saídas principais e dimensões.
- Amostras limitadas de texturas pequenas já previstas pelo capturador do SDK.
- `game.log`: log da execução que gerou o relatório.

O ZIP não inclui ISO, ELF, saves nem código recompilado. Os diagnósticos podem
conter imagens do jogo; não são incorporados ao código nem publicados no GitHub.
Falhas e tentativas incompletas ficam preservadas para análise.

## Limites e próximo passo

Esta etapa registra os draws existentes; não produz motion vectors, profundidade
linear para DLSS, camera jitter nem histórico de objetos. O hash do VS não prova
que um draw pertence à HUD. Identificar a HUD exige comparar capturas de telas
diferentes e confirmar texturas, estado de profundidade, blend e transformações.
Até essa confirmação, o renderizador não reordena nem separa os desenhos.

CI testa os eventos, o limite, caminhos Unicode e os marcadores, compila o
adaptador completo e repete os testes GPU de FSR. Capturas reais do jogo precisam
ser validadas no computador do usuário.
