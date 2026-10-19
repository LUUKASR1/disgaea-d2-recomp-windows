# Desenvolvimento 0.1.59 — visão do laço principal

A comparação 0.1.58 não demonstrou benefício da preparação de atributos:
58,29 → 58,04 callbacks/s, P95 20,77 → 21,15 ms e P99 25,47 → 25,75 ms.
As cargas diferiram; isso não prova regressão. A opção continua desligada por
padrão e não é promovida ao canal estável. Os picos de 149–163 ms com callback
gráfico abaixo de 0,5 ms também não identificam a causa da pausa inteira.

## Integração

O port já tinha `D2_LOOP_FRAME`, desligado no perfil e fora dos ZIPs. Esta build
integra essa medição ao modo de desempenho. O header do port usa agora o caminho
UTF-8 da sessão e a abertura Unicode já utilizada pelo runtime. F9 passa a ser
rotulado Mothman, e os atalhos só são consultados com uma janela do processo do
jogo em foco. Não há alteração de esperas, apresentação, relógio, comandos ou
sincronização. SDK/lock e versão pública 0.2.1 permanecem intactos.

`frames.csv.loop.csv` registra até 18.000 intervalos do laço, com estas seções
sequenciais: espera por evento/deadline, atualização do relógio/eventos GCM,
submissão/apresentação guest, drain FIFO, processamento de mensagens e restante.
São os pontos de medição que já existiam no laço; não são duração de GPU nem
utilização de CPU. Espera inclui ociosidade intencional e agendamento do sistema.
`present_ms` inclui a submissão inteira, não apenas DXGI Present.

O resumo acrescenta `render_loop`, máximos, P95, contagem de esperas maiores que
o timeout solicitado +1 ms e os dez maiores intervalos. Essa contagem não prova
refresh perdido. A diferença entre a soma das seis seções e o intervalo é
registrada para conferir a cobertura. Não somar essas seções com tempos do
backend ou dos callbacks amostrados, pois são medições sobrepostas.

Os índices de quadro e a consulta dos atalhos são independentes de `frames.csv`:
os arquivos terminam em pontos distintos do fluxo e não devem ser unidos por
número de linha. O custo adicional da instrumentação no PC real não foi medido.
O modo normal remove flags herdadas e mantém a medição desligada. Arquivos
antigos continuam aceitos; loop ausente ou inválido é explicitamente indicado,
sem impedir o relatório principal nem empacotar arquivo adicional inválido.

## Teste

Na 0.1.59: medição ligada, reaproveitamento de texturas ligado, preparação de
atributos desligada, imagens desligadas. Mantenha as opções gráficas. Aqueça o
ataque do Mothman uma vez; F9, repita por 30–60 segundos, F10 antes de sair da
batalha e feche normalmente. Envie um ZIP. Não é necessário repetir OFF/ON.

## English

Development 0.1.59 integrates the port's existing render-loop timing into the
performance ZIP. It adds no scheduling or game-timing changes. The extra CSV
uses the Unicode session path and records wait, tick, whole guest submission,
FIFO drain, message-pump and other host sections. Normal play remains inactive.

Loop and visible-callback frame indices are independent; never join by row
number. Wait time can be intentional idle or OS scheduling. Present includes
the whole guest submission. Categories overlap backend measurements and are not
CPU utilization or GPU time. Missing/invalid loop reports are explicitly marked.
The public 0.2.1 release is unchanged.

Enable measurement and texture reuse, disable vertex preparation and image
captures, warm up Mothman's attack, then F9, repeat for 30–60 seconds, F10 before
leaving battle and close normally. Send one ZIP; no OFF/ON comparison is needed.
