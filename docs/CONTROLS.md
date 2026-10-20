# Comandos / Controls

## Português

O launcher tem uma página **Controles**, também acessível pelo botão **Guia de
comandos** no início. O guia acompanha o idioma selecionado, pode ser consultado
enquanto o jogo está aberto e usa títulos com contorno, dourado e painéis roxos
inspirados na apresentação do jogo. Usa fontes instaladas no Windows; não inclui
fontes extraídas do jogo.

O teclado atual substitui o controle da porta 0 quando não há controle XInput
conectado nessa porta e a janela do jogo tem foco.

| Tecla | Equivalente do controle |
| --- | --- |
| Setas | Direcional e analógico esquerdo |
| Z / X | Cruz / círculo (confirmar / voltar, conforme o contexto) |
| A / S | Quadrado / triângulo |
| Q / W | L1 / R1 |
| 1 / 2 | L2 / R2 |
| Enter / Tab | START / SELECT |

Este guia não acrescenta remapeamento nem seleção direta pelo mouse dentro do
jogo. Os cliques do launcher funcionam normalmente.

### Próxima etapa: ponteiro dentro do jogo

A auditoria encontrou a leitura do controle no runtime e um gancho de
`cellPadGetData` na thread PPU usado por ferramentas do port. Ainda não identificou
endereços validados para o cursor de batalha, suas casas selecionáveis ou os
retângulos dos itens de menu. Os endereços de câmera, unidades e cenário existentes
não substituem essas informações.

Antes de habilitar a opção, precisamos localizar e validar a seleção no executável
USA 1.40, converter coordenadas da janela para a área renderizada (incluindo DPI e
barras), resolver o alvo por contexto e aplicar a ação na thread do jogo. Os testes
precisam cobrir menus, quatro orientações da câmera, casas ocupadas e vazias,
cancelamento, mudança de resolução e perda de foco. Não simular uma seleção direta
apenas associando o clique ao botão de confirmar.

## English

The launcher includes a **Controls** page, also accessible through **Command
guide** on the home page. It follows the selected language and remains accessible
while the game runs. Outlined gold headings and purple panels are inspired by the
game's presentation, using installed system fonts rather than extracted game fonts.

Keyboard fallback replaces port 0 when no XInput controller is connected to that
port and the game window has focus.

| Key | Controller equivalent |
| --- | --- |
| Arrow keys | D-pad and left stick |
| Z / X | Cross / circle (confirm / back, depending on context) |
| A / S | Square / triangle |
| Q / W | L1 / R1 |
| 1 / 2 | L2 / R2 |
| Enter / Tab | START / SELECT |

The guide does not add remapping or direct pointer selection inside the game.
Launcher buttons support normal mouse clicks.

Direct pointer selection requires validated menu and battlefield selection hooks
for the USA 1.40 executable. Existing camera/unit addresses do not establish those
hooks. Window coordinates must account for DPI, scaling and letterboxing; selection
must execute on the game thread. Validation must cover menus, four camera
orientations, occupied/empty tiles, cancellation, resolution changes and focus loss.
Mapping a click to the confirm button alone does not implement direct selection.
