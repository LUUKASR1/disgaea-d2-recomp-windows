# Desenvolvimento 0.1.57 — amostragem dos callbacks

O teste 0.1.56 do Mothman teve 1.784 quadros em 30,78 s, média 57,95 callbacks/s,
P99 de 25,52 ms e máximo 33,25 ms. A espera máxima medida pelo lock do FIFO foi
0,0018 ms. Nos quadros acima de 20 ms, o drain mediu 7,07 ms em média, contra
3,06 ms nos demais. Isso não explica o intervalo inteiro nem prova gargalo de
GPU ou CPU. Não há evidência para alterar a sincronização neste passo.

Esta build acrescenta diagnóstico aos pontos de callback que já existiam no
SDK. Não modifica comandos, vértices, constantes, uploads, locks ou relógios do
jogo; não promete ganho de desempenho. A adaptação continua na cópia de build,
sem alterar SDK/lock. A versão pública 0.2.1 não muda.

## Coleta e interpretação

Ao marcar o primeiro cenário, começa uma amostragem determinística de
aproximadamente 1/256 das chamadas de cada categoria: seleção de vertex shader,
constantes, associação de textura, draw arrays, draw indexed e vertex fetch.
Chamadas não selecionadas atualizam contador/gerador, sem relógio. Não há texto
por desenho, imagens ou resumos periódicos. A amostragem para no F10, no limite
de 18.000 quadros ou ao salvar na saída. Sem medição, ela fica inativa.

`frames.csv.fifo.json` é salvo junto ao CSV e incluído no ZIP; `summary.json`
acrescenta médias das chamadas amostradas e totais extrapolados. Os resultados
combinam todos os cenários marcados, sem atribuição por quadro. As categorias
são inclusivas e aninhadas: não somar tempos. Menos de 30 amostras é destacado
como evidência fraca; 30 não garante precisão estatística. A amostragem pode
perder eventos raros e acrescenta overhead ainda não medido no PC de teste.
Uma chamada externa ainda em execução ao salvar não é amostra concluída naquele
snapshot. Contagens e estimativas não são utilização de CPU nem tempo de GPU.

Os formatos anteriores de relatório continuam aceitos. O analisador limita e
valida o arquivo adicional; arquivo ausente não vira resultado zero, e arquivo
inválido é identificado sem interromper o relatório principal.

## Teste

Abra a 0.1.57 e aguarde a preparação. Mantenha as opções gráficas anteriores,
medição ligada, imagens desligadas e reaproveitamento de texturas ligado.
Aqueça o ataque do Mothman uma vez. Pressione F9, repita o ataque durante cerca
de 30 segundos, F10 antes de sair da batalha e feche normalmente. Envie um ZIP.
Esta execução serve para localizar custos; não deve ser usada como comparação
direta de FPS com uma build sem essa amostragem.

## English

Development 0.1.57 samples approximately 1/256 of calls at six existing FIFO
callback boundaries. It is diagnostic, not a performance fix. It preserves game
timing, rendering and synchronization. Sampling begins with the first marked
stage and stops on save, F10 or the frame limit; ordinary play is inactive.
The additional bounded JSON report combines all marked stages. Categories are
inclusive and nested, so do not sum estimates. Rare events may be missed and
target-PC overhead is unmeasured. Low sample counts are flagged; 30 samples is
not a statistical accuracy guarantee. Missing reports are not treated as zero.

Enable measurement and texture reuse, disable images, warm up Mothman's attack,
then press F9, repeat attacks for about 30 seconds, press F10 before leaving
battle and close normally. Send one ZIP. This diagnostic run should not be used
as a direct FPS comparison against an unsampled build.
