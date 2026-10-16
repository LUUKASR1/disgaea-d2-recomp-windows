# DirectX 12 — medição de desempenho da build de desenvolvimento

Esta etapa coleta uma referência para decidir onde otimizar. Não há ganho de FPS confirmado nem alteração das políticas de buffers, sincronização, renderização ou ritmo do jogo. A versão pública 0.2.0 permanece intacta.

## Como testar

1. Abra o launcher de desenvolvimento **0.1.52-performance-preview**. Em **Arquivos e suporte**, marque **Medir na próxima abertura do jogo — sem imagens**. Deixe as capturas de desenvolvimento desativadas; os dois modos não podem ser usados juntos.
2. Mantenha a resolução, FSR, HUD, VSync e Hz iguais nos testes que deseja comparar. Na primeira execução após atualizar as fontes, aguarde a compilação local automática.
3. Entre no Castle Hallway, deixe a janela do jogo em foco e pressione **F6**. Jogue por aproximadamente 30 segundos. A coleta não começa na tela de loading; começa quando você marca um cenário.
4. Numa batalha comum, pressione **F7** e jogue por aproximadamente 30 segundos.
5. Antes de observar o ataque do Mothman, pressione **F9**. Observe algumas execuções, se possível por aproximadamente 30 segundos. Se não houver Mothman disponível, envie primeiro os outros cenários; não é preciso repetir a campanha para completar a coleta.
6. Pressione **F10** para encerrar a coleta. O jogo continua aberto. Feche o jogo normalmente para o launcher resumir os dados e gerar `DisgaeaD2-Desempenho-<data>-<id>.zip` em Downloads. Se Downloads não estiver disponível, o relatório fica no perfil do aplicativo. O botão **Abrir pasta do relatório de desempenho** abre o destino real.

O limite é 18.000 amostras por sessão. CSV e dados da sessão ficam em `%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview\performance`. Sem cenário marcado, o relatório diz `no-samples`: isso não é uma medição bem-sucedida. Para jogar sem medir, desmarque a opção. A edição oficial não expõe essa opção.

## O que o relatório permite avaliar

- Intervalos entre callbacks de renderização visíveis: média, P50, P95, P99 e quadros acima de 20/33,34/50 ms.
- Tempo de parede no callback do renderizador, na preparação de recursos/pipelines, no caminho de espera pelas fences e na chamada de apresentação.
- Tempo do host nos drenos FIFO concluídos, contagem de desenhos e faltas no cache de pipelines de fragmentos.
- Tempo gasto nas faltas desse cache; ele não representa toda a compilação de shaders.

**Não mede utilização de CPU/GPU nem duração de execução da GPU.** Uma espera pela fence inclui sinalização, consulta e possível bloqueio do host; não prova, sozinha, que a GPU é o gargalo. Algumas categorias se sobrepõem e podem envolver outras threads: não some os tempos nem os converta em percentuais de uso da CPU. `render_host_ms` não cobre sozinho todo o trabalho fora da tela ou a execução do código recompilado.

O coletor existente foi adaptado numa cópia de build auditada, preservando o SDK original. As amostras ficam em memória; não há imagens, leitura de pixels, logs por desenho ou ordenação de percentis a cada cinco segundos. O CSV é gravado ao concluir ou fechar. O custo da instrumentação no PC do usuário ainda precisa ser comparado com a opção desligada.

Compartilhe o ZIP para análise. Ele contém apenas `session.json`, `summary.json` e, se houver amostras, `frames.csv`: não inclui assets, shaders, saves ou o log completo do jogo.

## English

Development launcher 0.1.52 adds an opt-in, bounded DirectX 12 host-frame baseline. The public 0.2.0 release is unchanged; no performance improvement is claimed yet.

Enable **Medir na próxima abertura do jogo — sem imagens** under **Arquivos e suporte**, with image captures disabled. Keep graphics settings consistent. With the game window focused, use **F6** for Castle Hallway, **F7** for ordinary battle and **F9** for Mothman's attack. Observe roughly 30 seconds per available scenario. **F10** finishes collection; close the game normally to produce the ZIP in Downloads, or the application profile if that directory is unavailable. The report-folder button opens the actual destination. Maximum: 18,000 samples.

The report covers callback intervals, host wall times for rendering/preparation/fence waits/presentation, completed FIFO-drain time and fragment-pipeline cache misses. **These are not GPU execution times or CPU/GPU utilization measurements.** Categories may overlap; never add them to infer utilization. Missing samples are explicitly reported. No image readbacks, per-draw clocks or five-second percentile sorting are enabled. Instrumentation overhead on the target PC has not been established.
