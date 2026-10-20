# Desenvolvimento 0.1.56 — espera da fila

Esta build acrescenta diagnóstico; não promete ganho de FPS nem altera locks,
vblank, apresentação, relógio do jogo ou execução de comandos. A versão pública
0.2.1 permanece separada.

As duas capturas curtas do Mothman da 0.1.55 confirmam menos criações de textura
por mil quadros (76,61 → 27,61), mas não ganho de FPS médio (56,36 → 55,12).
Elas têm 8,80 e 5,91 segundos e cargas diferentes. Nos piores quadros, o callback
visível fica abaixo de 1,3 ms e o FIFO mede cerca de 8–10 ms. Isso não identifica
sozinho o trabalho que ocupa o restante do intervalo nem prova gargalo de CPU.

## Novas medidas

- `fifo_lock_wait_ms`: tempo de aquisição do lock do FIFO, incluindo overhead
  das chamadas de medição. Duas leituras de relógio por chamada, nenhuma por
  comando ou desenho. A execução dentro do lock já era medida separadamente.
- `fifo_lock_calls`: chamadas que adquiriram o lock.
- `fifo_flip_held_calls`: dessas chamadas, quantas encontraram um flip já
  alcançado e por isso não executaram o drain naquele ponto. Não é duração da
  retenção e não prova problema de sincronização.

Os contadores usam operações atômicas entre threads. As trocas individuais no
callback dão atribuição aproximada por quadro; não são um snapshot coerente de
um drain. Os tempos podem se sobrepor a outras categorias; não devem ser somados
para inferir utilização ou tempo restante. A captura continua limitada a 18.000
quadros, sem imagens. O analisador aceita CSVs antigos de 13 e 25 colunas e os
novos de 28, inclui contexto dos picos e taxas normalizadas após a coleta.

## Teste

Abra a 0.1.56, aguarde a preparação automática e mantenha as opções gráficas.
Ative a medição, desligue capturas de imagens e ligue reaproveitamento de
texturas. Aqueça o ataque do Mothman uma vez. Use F9, repita o mesmo ataque
algumas vezes e F10 antes de sair da batalha. Feche normalmente e envie o ZIP.
Não precisa repetir o teste OFF/ON neste passo.

## English

Development 0.1.56 adds coarse FIFO lock diagnostics, not a performance fix.
It preserves synchronization, game timing and command execution. Measurement
adds two clock reads per FIFO entry, with no per-command or per-draw clocks.
Atomic counters provide approximate frame attribution across threads. Held-flip
counts measure visits, not held duration. Host categories can overlap and are
not GPU timings or CPU utilization. Older report formats remain supported.

Enable measurement, disable image captures, enable compatible texture reuse,
warm up the Mothman attack once, then use F9, repeat attacks and press F10 before
leaving battle. Close normally and send the ZIP. One run is sufficient for this
diagnostic step; the public 0.2.1 release is unchanged.
