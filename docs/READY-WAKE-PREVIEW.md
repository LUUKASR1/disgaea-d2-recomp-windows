# Desenvolvimento 0.1.61 — quadro elegível antes da espera

A coleta 0.1.60 de 19/10 registrou 2.435 quadros em 41,93 s (58,07 FPS),
P99 de 24,77 ms e máximo de 33,86 ms, sem intervalos acima de 50 ms.
Quatro intervalos do laço próximos de 33 ms incluíram esperas de 16,88–17,70 ms
classificadas como elegíveis na entrada. Isso justifica testar o guard existente,
mas não prova que toda a duração foi desperdício: a classificação é um snapshot.
Outros picos coincidiram com preparação e processamento da FIFO.

Esta preview ativa explicitamente `D2_GCM_READY_WAKE=1` ao iniciar o jogo,
com ou sem medição. O canal oficial continua forçando a opção desligada.
Não há nova configuração persistente nem alteração do SDK fixado.
O relatório registra `settings.ready_wake_experiment=true`.

O guard existente verifica sob o lock de flip o mesmo predicado usado para
selecionar a apresentação. Libera o lock e retorna sem esperar somente quando
já elegível. Não consome eventos, flips ou tickets; não altera vblank ou o
relógio de 60 Hz. A elegibilidade volta a ser verificada no fluxo normal.
Não elimina a janela de corrida entre a verificação e o início da espera.
A opção de apresentação ilimitada continua independente.

Os testes Win32 compilam a função real extraída da cópia adaptada e cobrem
guard ligado/desligado, regras de FIFO/tickets/relógio e apresentação ilimitada,
retenção do evento no retorno antecipado, liberação do lock e estado preservado.
Isso não substitui o teste do jogo nem garante 60 FPS constantes.

## Teste no jogo

Mantenha as mesmas opções da coleta anterior: medição e reaproveitamento de
texturas ligados; atributos por desenho e imagens desligados. O ajuste de
espera já está ligado nesta preview. Aqueça Mothman uma vez, F9, repita por
30–60 segundos e F10 antes de sair da batalha. Confira também velocidade,
áudio e controles. Feche normalmente e envie um ZIP. Não atualizar a versão
pública estável até comparar o resultado e verificar ausência de regressões.

## English

Development 0.1.61 explicitly enables the existing ready-before-wait guard in
preview launches, including ordinary play; official launches force it off.
Session metadata records `ready_wake_experiment`. No saved setting or pinned
SDK is changed. The guard releases the flip lock and skips the event wait only
when the existing selection predicate already permits presentation. It does
not consume events, retire flips, bypass FIFO/tickets, or change the 60 Hz clock.
The check-to-wait race is not eliminated. Unlimited presentation remains independent.

The 0.1.60 capture had four roughly 33 ms loop intervals containing 16.88–17.70 ms
waits classified eligible at entry. This motivates the experiment, not a proven
performance fix; readiness is a snapshot and other CPU-side spikes remain.
Native tests exercise the actual adapted wait function with the guard on/off,
event preservation, lock release, unchanged queue state and timing prerequisites.

Keep measurement and texture reuse on, vertex preparation and images off.
Warm up Mothman once, F9, repeat for 30–60 seconds, F10 before leaving battle.
Check game speed, audio and controls, close normally and send one ZIP.
Stable public release remains unchanged pending comparison and regression checks.
