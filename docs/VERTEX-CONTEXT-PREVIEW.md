# Desenvolvimento 0.1.58 — preparar atributos por desenho

O relatório 0.1.57 do Mothman registrou 46.334.892 chamadas de leitura de vértices
em 37,18 s. As estimativas amostradas foram 2,94 s nas leituras, dentro de 3,88 s
nos desenhos não indexados. São categorias aninhadas; não somar. A duração média
de uma leitura é próxima do custo do cronômetro, portanto a estimativa não prova
um gargalo nem prevê ganho de FPS. O teste teve média 58,22 callbacks/s e máximo
34,08 ms por quadro.

## Alteração

Esta build expõe, de forma independente e desligada por padrão, o caminho
`D2_VERTEX_CONTEXT` que já existe no SDK fixado. Não reescreve o decodificador.
O caminho prepara a lista de atributos ativos e os registradores constantes
uma vez por upload. Para cada vértice, percorre essa lista em vez de verificar
todos os 16 atributos. Os atributos ativos continuam usando o decodificador
original e a resolução original de endereços, inclusive DMA local/main,
frequência, formatos e preenchimento das componentes ausentes.

Não há cache de vértices decodificados, endereços ou dados entre uploads. O
estado é reconstruído em cada upload; dados ativos são lidos da memória guest.
A análise presume estado estável durante uma chamada síncrona de upload, como
o caminho já documentava. Quads já reutilizam seus quatro vértices para formar
dois triângulos, por isso essa montagem não foi modificada.

Testes executam o helper e o decodificador reais do SDK contra a montagem
original, comparando os 256 bytes de cada vértice. Incluem 7.200 vértices com
estados variados, formatos, tamanhos, divisores, offsets, registradores com
bits arbitrários, arrays desativados, stride zero, memória alterada em novo
upload, estado diferente e memória indisponível. Isso não substitui teste
visual nem estabelece ganho de desempenho no PC do usuário.

## Comparação

1. Em **Arquivos e suporte**, mantenha medição e reaproveitamento de texturas
   ligados e imagens desligadas. Não altere resolução, Hz ou outras opções.
2. Deixe **Preparar atributos por desenho — experimental** desligado. Aqueça o
   ataque do Mothman uma vez; F9, repita por cerca de 30 segundos, F10 antes de
   sair da batalha e feche normalmente.
3. Reabra com a nova opção ligada e repita. Envie os dois ZIPs e informe se
   percebeu defeitos em cenário, personagens, textos ou efeitos.

`settings.vertex_context_experiment` identifica a escolha no relatório. Ela
vale por abertura, não altera o JSON gráfico e ignora flags herdadas do shell.
O canal oficial força a opção desligada. A build pública 0.2.1 não muda. A
amostragem 0.1.57 continua igual nos dois lados desta comparação.

## English

Development 0.1.58 exposes the pinned SDK's existing per-upload attribute
preparation path as an independent, opt-in experiment. It builds the active
attribute list once per upload, avoiding a 16-attribute scan per vertex. Active
data still uses the original decoder and address resolution. There is no
decoded-data or address cache across uploads. The SDK and its lock are intact.
Byte-for-byte equivalence tests do not establish real-game performance or visual
correctness. The public 0.2.1 release is unchanged.

Keep measurement and texture reuse enabled, image capture disabled, and all
graphics settings identical. Measure Mothman's attack with **Prepare vertex
attributes per draw — experimental** off, then close and reopen with it on.
Warm up once, F9, repeat for about 30 seconds, F10 before leaving battle, then
close normally. Send both ZIPs and report any visual issues. Reports record
`vertex_context_experiment`. The option defaults off and is hidden/disabled in
the official channel. Sampling remains identical in both runs.
