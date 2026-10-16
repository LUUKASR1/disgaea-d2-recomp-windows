# Análise das primeiras capturas de HUD — 0.1.15

As cinco capturas de 13/10/2026 foram concluídas em 1280×720 interno/saída,
sem upscaler ativo. O log termina com encerramento do processo e código 0.
Foram inspecionadas as imagens e os manifestos completos. Não havia alvos
offscreen listados nestes frames. O relatório recebido não é publicado aqui.

| Captura | Cena observada | Draws | Primeiro candidato da HUD | Draws no bloco final |
| --- | --- | ---: | ---: | ---: |
| 00 | Título e menu inicial | 28 | Sem corte automático | — |
| 01 | Castle Hallway | 804 | 801 | 3 |
| 02 | Batalha com menu Execute aberto | 308 | 287 | 21 |
| 03 | Ataque de Laharl, dano 21 | 345 | 336 | 9 |
| 04 | Ataque do Mothman, dano 1 | 369 | 366 | 3 |

## Evidência relevante

O VS registrado como `2003CB89` aparece tanto na interface como em personagens,
sombras e partículas. Antes dos blocos finais acima, há respectivamente 9, 25,
24 e 44 draws com esse mesmo VS. Filtrar apenas por shader removeria conteúdo
da cena. O estado de blend também é compartilhado. O teste de profundidade
permanece habilitado em muitos draws da interface; desligamento do depth test
não serve como identificação de HUD.

Nestes quatro frames de gameplay, os candidatos finais combinam alvo de
display, VS `2003CB89`, três vértices amostrados com w=1 e z entre 0 e 0,99,
e posição no bloco contíguo final do frame. Personagens e partículas que usam
esse VS antes da HUD têm z próximo de 0,999 nas amostras observadas. Geometria
com o outro VS não deve ser classificada pelo z bruto, pois passa por outra
transformação. Endereços de texturas e de FP não são tratados como constantes
de identidade entre execuções.

A textura de texto 1024×280, formato A3, aparece no título da sala e nos menus
da batalha. Outros atlas desenham painéis, ícones e números. As posições
amostradas correspondem às regiões visíveis da interface. Os efeitos dos golpes
aparecem antes do bloco final; os números de dano e TOTAL DAMAGE ficam nele.

O título usa VS `0E216F34` nos 28 draws, inclusive o fundo. Um corte baseado
nesse shader não separa fundo e menu. Nesta primeira etapa, o título deve manter
o caminho original até possuir uma regra própria validada.

## O que ainda não está provado

O capturador registra apenas três vértices por draw. Esses dados não confirmam
todos os vértices de um batch nem a contribuição individual do draw no framebuffer.
Os números da tabela são **candidatos**, não uma separação já implementada ou
validada. Não há motion vectors, histórico de câmera ou profundidade linear
para uma integração temporal neste relatório.

## Implementação a validar em seguida

1. Confirmar geometria completa dos candidatos e produzir imagem da cena sem
   esse bloco, imagem da HUD e composição final na mesma captura.
2. Manter a ordem original dos draws da HUD e seus estados de blend, depth e
   stencil. Não retirar personagens/partículas identificados pelo mesmo shader.
3. Inserir o upscaling entre a cena e a HUD, desenhando a interface na resolução
   de saída. Cenas sem uma fronteira segura usam o caminho original.
4. Comparar a composição com a referência em resolução nativa e repetir
   menus, efeitos, save/load e fechamento antes de tornar o caminho padrão.

`tools/analyze_frame_capture.py` faz análise offline do ZIP e gera JSON, sem
extrair imagens ou alterar renderização. Os testes são sintéticos e verificam
que partículas, geometria desconhecida, alvos offscreen, draws não finais e
o título não sejam classificados indiscriminadamente como HUD.
