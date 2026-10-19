# Desenvolvimento 0.1.60 — motivos das esperas

Na coleta 0.1.59, o maior intervalo do laço foi 161,62 ms, dos quais 159,83 ms
foram esperas acumuladas. Foram 45 chamadas; a maior durou 18,29 ms. Portanto,
não houve uma única chamada de espera de 160 ms. Isso localiza o tempo, mas não
prova ociosidade evitável, falha de GPU ou a causa do atraso do produtor.

## Classificação

Somente durante a medição do laço, a cópia de build do GCM tira um snapshot sob
o lock de flip e o libera antes de chamar o `WaitForSingleObject` original, com
os mesmos argumentos. Não há consumo de flip, alteração de tickets, avanço de
vblank ou desvio do predicado de seleção. O SDK/lock e a versão pública 0.2.1
permanecem intactos. O predicado usa a mesma função de elegibilidade do port.

| Coluna de motivo | Estado na entrada da espera |
|---|---|
| `no_pending_flip` | Não há flip pendente |
| `fifo_pending` | A cabeça da fila ainda aguarda seus comandos |
| `ticket_order` | Contadores de apresentação e conclusão não coincidem |
| `clock_gate` | Os pré-requisitos anteriores passaram, mas o relógio bloqueia a seleção |
| `eligible_at_entry` | O predicado de seleção já era verdadeiro |

Os motivos são exclusivos e seguem essa prioridade. O arquivo de loop acrescenta
tempo e contagem por motivo e resultados do evento: sinal, timeout e outro.
São tempos de chamadas de espera concluídas associadas ao estado inicial,
não duração comprovada da condição. O estado pode mudar logo após o snapshot.
Esperas incluem ociosidade intencional e agendamento do sistema. Uma espera
classificada como elegível pode terminar imediatamente com o evento sinalizado;
sua contagem isolada não prova tempo desperdiçado.

O controle da coleta é desligado ao salvar, inclusive sem amostras. Fora do modo
de medição, não há snapshot ou relógios por espera. As somas são coletadas com
operações atômicas; trocas individuais têm atribuição aproximada se houver
chamadas concorrentes. O custo da instrumentação no PC real não foi medido.

Os relatórios de loop antigos continuam aceitos e são identificados sem detalhe
de motivos. Os índices continuam independentes de `frames.csv`. Não somar estes
tempos com as categorias sobrepostas do backend. Os testes nativos verificam
combinações do predicado, sinais/timeouts/erro reais, coleta desligada e o CSV.

## Teste

Na 0.1.60, mantenha medição e reaproveitamento de texturas ligados, atributos por
desenho desligados e imagens desligadas. Aqueça Mothman uma vez, pressione F9,
repita por 30–60 segundos, pressione F10 antes de sair da batalha e feche
normalmente. Envie um ZIP. Ainda é diagnóstico; não promete ganho de FPS.

## English

Development 0.1.60 snapshots flip readiness under the flip lock, releases it,
then performs the original event wait with unchanged arguments. It records
exclusive entry reasons and wake/timeout/other outcomes during measurement.
No flip consumption, ticket, vblank or eligibility behavior is changed.

Reasons describe entry state, not how long that state remained true. Eligible
waits can return immediately; counts alone do not establish wasted time.
Timing includes intentional idle and host scheduling. Atomic exchanges have
approximate attribution with concurrent callers. Target-PC overhead is unmeasured.
Old loop reports remain supported. Collection stops on save, including empty
sessions, and ordinary play has no per-wait snapshot or clock instrumentation.

Keep measurement and texture reuse on, vertex preparation and images off. Warm
up Mothman once, F9, repeat for 30–60 seconds, F10 before leaving battle, then
close normally. Send one ZIP. This is diagnostic, with no FPS gain claimed.
