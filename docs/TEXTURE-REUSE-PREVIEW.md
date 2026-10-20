# Reaproveitamento de texturas — desenvolvimento 0.1.55

O relatório da 0.1.53 tinha 9.640 amostras. Em 55 dos 56 quadros com preparação acima de 8 ms também ocorreram novas alocações; isso é uma correlação, não prova da causa completa. O pico de 653 ms não é explicado apenas pelos medidores do renderizador. A hipótese desta build é reduzir parte dos custos de criação de recursos sem aumentar a capacidade do cache.

## O que muda

- A busca original por chave e a verificação de conteúdo continuam primeiro. Um acerto ou atualização da própria entrada mantém o caminho original.
- Só em uma falta nessa busca, a opção pode escolher outra alocação existente, com textura e buffer de upload disponíveis, mesmas dimensões, formato completo e tipo 2D/cubo.
- A candidata não pode estar usada no quadro nem conter uma chave que aparece nos desenhos restantes. A reserva considera a lista completa de desenhos antes dos uploads; colisões e chaves duplicadas são conservadoramente preservadas.
- Sem candidata compatível, o backend usa o slot que já escolheria antes, inclusive o comportamento original quando falta espaço. São os mesmos 96 slots, sem novo pool de texturas. Não há mudança nas esperas de GPU, conversão dos texels, checksums ou barreiras de upload.
- Pré-requisito de correção: com a opção ligada, os SRVs auxiliares de textura ficam depois das tabelas por desenho. O endereço antigo `1 + slot` podia se sobrepor às tabelas a partir do descritor 32. O heap reserva 96 descritores adicionais; isso não cria 96 novas texturas. Com a opção desligada, os endereços e a política de seleção anteriores são mantidos.

A adaptação ocorre só na cópia auditada de build; SDK e lock permanecem intactos. A versão pública 0.2.1 não é alterada. Testes da política e compilação do adaptador não substituem teste visual no jogo real. Não há ganho de FPS demonstrado ainda.

## Comparar no mesmo PC

1. Abra a 0.1.55 e mantenha resolução, Hz, VSync, VRR, FSR e HUD iguais nos dois testes. Aguarde a recompilação local automática nesta primeira abertura.
2. Em **Arquivos e suporte**, deixe **Reaproveitar texturas compatíveis — experimental** desligado e ative a medição sem imagens. Jogue os trechos habituais: F6 Castle Hallway, F7 batalha, F9 Mothman; F10 conclui. Feche normalmente para gerar o ZIP.
3. Abra o jogo novamente com **Reaproveitar texturas compatíveis — experimental** ligado e repita os mesmos trechos. A escolha é por abertura e não altera o JSON das opções gráficas. Evite misturar outras ações/cenas dentro de um trecho marcado.
4. Compare os dois ZIPs, que identificam `settings.texture_reuse_experiment`. Priorize contagens de criação de textura/buffer, picos de preparação e P95/P99 dos intervalos, além de conferir textos, efeitos e transições. Se aparecer defeito visual, desligue a opção e reabra o jogo.

A opção começa desligada em cada abertura do launcher, não é exposta na interface oficial e ignora flags herdadas do shell. A medição continua sem imagens ou clocks por desenho; tempos de host não são utilização de CPU/GPU nem duração de execução da GPU.

## Resultado inicial no PC de teste — 16/10/2026

O usuário confirmou funcionamento sem defeitos percebidos. As sessões `071640-4315547b` (desligado) e `072014-fbbd6847` (ligado) têm durações diferentes; as contagens abaixo são normalizadas por mil quadros.

| Trecho | Criações de textura OFF → ON / mil quadros | P99 OFF → ON (ms) |
|---|---:|---:|
| Castle | 74,26 → 35,05 | 24,44 → 19,33 |
| Batalha | 114,68 → 79,63 | 26,70 → 26,11 |
| Mothman | 179,54 → 80,71 | 25,89 → 28,44 |

Não ocorreram falhas de criação de recursos. Isso demonstra redução observada de alocações, não estabilidade garantida ou ganho geral de FPS.

Nos dois arquivos, os grandes picos aparecem no padrão de draws `1 → 21 → 17 → 17 → 294`. No teste ligado, o quadro 4883 demora 153,07 ms entre callbacks, mas o callback medido demora 0,45 ms, sem criação de textura ou compilação. Não é possível atribuir esse intervalo inteiro à preparação gráfica. A ação do jogador não é identificada pelo CSV: a sequência sugere uma transição, mas não a prova.

No trecho Mothman ligado, o quadro 6681 registra 40,85 ms entre callbacks e 25,01 ms de preparação, com sete criações de textura, sem compilação. Existem padrões diferentes a investigar. A próxima comparação deve repetir o mesmo ataque, marcando F9 imediatamente antes e F10 após algumas repetições, sem sair da batalha. Fazer ambas as execuções após um aquecimento equivalente para reduzir diferenças de compilação inicial.

O analisador passa a incluir cinco quadros de contexto por pico (dois anteriores e dois posteriores, quando disponíveis) e contagens por mil quadros. Esse processamento acontece após a coleta e aceita CSVs anteriores; não adiciona clocks, capturas ou processamento por draw ao jogo. Categorias de tempo podem se sobrepor e não devem ser somadas para calcular um suposto tempo restante.

## English instructions

Development 0.1.55 adds an opt-in comparison under **Files and support → Reuse compatible textures — experimental**. The public 0.2.1 release is unchanged. First launch rebuilds locally. Keep graphics settings identical, measure once with reuse off, close the game, then repeat with reuse on. Use F6 for Castle Hallway, F7 for battle, F9 for Mothman and F10 to finish; close normally to generate each ZIP. Reports record `settings.texture_reuse_experiment`. Check visual correctness as well as resource-creation counts, preparation spikes and callback-interval P95/P99. Avoid mixing scene transitions into an attack-only segment.

Exact-key hits and stale-entry refreshes retain their original paths. On misses, only idle allocations with matching dimensions, full format and cube type, both resources present, and no key needed later in the captured draw list are candidates. Otherwise, original slot selection and capacity behavior apply. Cache capacity remains 96 slots. Existing GPU waits, data conversion, checksums and upload transitions remain in place. When enabled, scratch SRVs move beyond per-draw descriptor tables to avoid aliasing; the heap grows by 96 descriptors, not textures. Disabled mode keeps original addresses and selection. The experiment defaults off per launcher session and inherited environment flags are cleaned.

The original SDK/lock are preserved. Policy tests and an adapter compile check do not establish real-game visual correctness or a performance improvement. The earlier allocation/spike correlation is not a complete explanation of stalls. Host wall timings are not CPU/GPU utilization or GPU execution duration.
