# Desenvolvimento 0.1.62 — estado da fila antes de esperar

A coleta 0.1.61 com reaproveitamento ligado (19/10 às 13:24) teve 2.968
quadros em 53,23 s: 55,76 FPS, P99 33,23 ms e máximo 39,51 ms. Houve uma
compilação de shader de cerca de 15,4 ms, sem falhas de recursos/PSO. Não é
uma comparação de carga idêntica com a 0.1.60 e não demonstra ganho global.
Esperas elegíveis praticamente desapareceram (uma de 0,0004 ms), enquanto
os maiores intervalos ainda incluíram espera sem flip pendente e trabalho FIFO.

## O que muda

Somente com a medição de loop ativa, a espera classificada `no_pending_flip`
recebe uma subcategoria. O snapshot do flip continua sob seu lock; depois de
liberá-lo são lidos estados atômicos já existentes e o PUT/GET da memória do
jogo. Não são adquiridos locks de cópia/render, consumidos eventos ou alterados
comandos. O ajuste de espera da 0.1.61 continua ligado na preview.

| Subcategoria | Observação na entrada, em ordem de prioridade |
|---|---|
| `legacy` | Staging de comandos no host desligado; não inferir estado do caminho legado |
| `label_wait` | Staging ligado e endereço de espera por label indicado |
| `host_queued` | Staging ligado e contadores de slots publicados/consumidos diferentes |
| `guest_put_get_differ` | Sem as condições anteriores, PUT e GET do jogo diferentes |
| `host_empty` | Nenhuma condição anterior; slots e PUT/GET observados iguais |

Essas leituras separadas NÃO formam um snapshot coerente da produção.
Contadores de slots não são contadores de desenhos nem de bytes; igualdade
não prova ausência de comandos sendo preparados em outra thread. Um endereço
de label não prova que o valor continua bloqueando. PUT/GET diferentes não
provam a causa do atraso da cópia. Os valores podem mudar entre leituras e
logo após a classificação.

A duração associada é a chamada de espera que terminou, não a duração de cada
condição nem tempo de CPU do produtor. São subdivisões das esperas sem flip:
não somar com elas. `host_empty` inclui ociosidade normal e não prova gargalo.
A coleta ajuda a decidir se o próximo diagnóstico deve observar publicação,
consumo ou trabalho anterior do jogo; ainda não mede todo o trabalho do jogo.

O CSV de loop acrescenta dez colunas (tempo/contagem para cinco estados).
Relatórios antigos continuam aceitos e identificados sem esse detalhe.
Contadores são tomados/resetados junto da coleta por quadro; operações
atômicas separadas têm atribuição aproximada se houver chamadas concorrentes.
Coleta para ao salvar/F10. Jogo normal não faz estas leituras adicionais.

Testes cobrem os cinco estados e sua prioridade na espera real instrumentada,
contagem ligada/desligada, reset, CSV nativo, validação/empacotamento Python
e compatibilidade com cabeçalhos antigos. Nenhuma nova promessa de FPS.

## Teste

Medição e reaproveitamento de texturas ligados; atributos por desenho e imagens
desligados. Aqueça Mothman uma vez, pressione F9 com o jogo em foco, repita por
30–60 segundos e pressione F10 antes de sair da batalha. Feche normalmente e
envie um ZIP. Não é necessário mudar as outras opções. Saves preservados.

## English

Development 0.1.62 subdivides completed no-pending-flip waits by separately
observed entry state: legacy staging disabled, label-wait address present,
host slots queued, guest PUT/GET different, or otherwise host empty.
No copy/render lock, queue mutation or event consumption is added. This is
measurement-only; the 0.1.61 ready-before-wait experiment stays enabled.

These are ordered, separate observations, not a coherent producer snapshot.
Slot counters are not draw/byte counters. A label address does not prove its
value remains blocking. PUT/GET differences do not establish the copying delay's
cause. Host-empty can be ordinary idle. Timings describe completed event waits,
not condition duration, producer CPU time or a proven stall; they are subsets
of no-pending-flip times and must not be added to them. Old reports remain valid.

The previous reuse-enabled run measured 55.76 FPS, P99 33.23 ms and maximum
39.51 ms; it did not demonstrate an overall improvement under matched workloads.
One shader compilation cannot explain all peaks. This diagnostic narrows which
producer/publication/consumer path to inspect next, without changing pacing.

Keep measurement and texture reuse on, vertex preparation and images off.
Warm up Mothman once, F9 while focused, repeat for 30–60 seconds, F10 before
leaving battle, then close normally and send one ZIP. No FPS gain is promised.
