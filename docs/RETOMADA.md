# Retomada do projeto — 14/10/2026

Pausa solicitada pelo usuário para retomar mais tarde em 14/10/2026 (horário de Brasília).

## Estado preservado
- Branch: `feature/native-graphics`.
- Implementação mais recente: `a745c593592236b15d8d9f2aeffd6eaec0a35a4d`.
- [PR #2](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/pull/2) aberto como rascunho. Não fazer merge nem publicar release estável.
- Preview: `0.1.28-geometry-raster-preview`.
- [Baixar o launcher 0.1.28](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/actions/runs/38504885705/artifacts/11866752729).
- Nome do executável: `DisgaeaD2Launcher-HUD-Output-Preview.exe`.
- O perfil existente de preview, a preparação e os saves são reutilizados; atualização dos fontes pode disparar rebuild automático do runtime.

## Validação
A build 0.1.28 passou os dez testes gráficos nativos em WARP, 62 testes Windows do launcher, verificações de preparação/interpreter/empacotamento incorporadas e testes nativos de ISO no Windows e Linux.

O usuário confirmou CPU/GPU para duas amostras na 0.1.25: erro máximo de aproximadamente 0,000057 pixel. Isso valida os pontos medidos, não um campo completo de vetores.

O teste real 0.1.27 não rasterizou nenhum draw. O JSON mostrou máscara 192 nas duas amostras: blend (64) e alpha test (128), com topologia triangular, viewport completo e stencil desligado. A 0.1.28 permite esses estados somente no diagnóstico de GEOMETRIA, mantendo todos os outros filtros e reportando explicitamente os estados não reproduzidos.

## Primeira ação ao retomar
1. Fechar o launcher anterior e abrir a preview 0.1.28.
2. Usar render e saída 1280×720, com upscaling desligado.
3. Habilitar capturas, entrar no Castle Hallway, pressionar F8 e girar a câmera em até dez segundos.
4. Aguardar a conclusão e analisar `motion-raster-check.json`, especialmente status, draw_count, covered_pixels, moving_pixels, invalid_pixels e sample_rejections.
5. Se houver cobertura, inspecionar `motion-coverage.bmp` e `motion-vectors.bmp`; elas representam o par congelado, que pode diferir do frame de output.bmp.
6. Desabilitar capturas para jogar normalmente. Compilação/readback do diagnóstico podem causar pausa durante a captura.

O usuário ainda não enviou um resultado real da 0.1.28. Não assumir que a captura vazia foi resolvida.

## Próximas etapas técnicas
- Validar direção, cobertura e projeção do raster geométrico parcial.
- Reproduzir alpha/discard e a contribuição dos shaders reais; verificar profundidade/oclusão da cena completa.
- Identificar histórico dos sprites/personagens e efeitos, com correspondência entre frames.
- Tratar cortes de câmera, disoclusão, jitter, convenções de depth e reset de histórico.
- Integrar DLSS SR somente com entradas válidas; depois Reflex e apresentação de Frame Generation.

## Limites atuais
DLSS SR e Frame Generation NÃO estão implementados. O raster de candidatos usa recursos próprios e depth LESS_EQUAL somente entre candidatos. Alpha/discard, blend e oclusão por objetos não amostrados não são reproduzidos. A máscara é geométrica, não a contribuição final da cena; motion_vectors_available e full_scene_visibility_validated permanecem false.

O render normal não é alterado pelo diagnóstico. Os adaptadores preservam o SDK auditado e seu lock. Não publicar assets, shaders ou código derivado do jogo. Manter o ritmo original: flips sem limite aceleraram o jogo no teste do usuário; Hz/VRR não alteram a simulação.

## Ambiente da colaboração
A compilação/verificação desta etapa ocorreu no GitHub Actions. O shell local da sessão estava indisponível por falta de espaço; não há controle direto do computador Windows do usuário. Buscar os fontes atuais no repositório e verificar o head antes de alterar a branch.
