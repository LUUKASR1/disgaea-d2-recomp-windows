# Candidata Windows 0.1 — 2026-10-12

Base: Sean13128/disgaea-d2-recomp, commit
`340cbed43cf8427005bcfcbf537e65190161ead8` na preparação original.
A coleta deve confirmar os commits e os arquivos atuais do computador.
Esta designação identifica uma candidata de preservação, não uma release publicada.

## Observado nos testes do usuário

- Windows x64, Ryzen 7 5700X3D, RTX 5070, 32 GB RAM.
- Jogo 1.40, renderer Direct3D 12, Clang 22.1.3, build Release.
- Textos corrigidos; correções de paleta e depth/stencil resolveram os
  artefatos relatados e a tela branca na seleção de ataque.
- Áudio e efeitos normais no último teste com SIMD.
- Saves receberam correção de substituição atômica no Windows; é necessário
  reconfirmar salvar, fechar, abrir e carregar na candidata preservada.
- Update/DLC usam dados locais do usuário. Não foi validado individualmente
  todo o conteúdo adicional.
- Cache da opção alpha melhorou o corredor. Agrupamento de partículas e
  reutilização de constantes estão ativos.
- Diagnósticos detalhados prejudicam o desempenho; uso normal os desliga.

## Desempenho

| Captura | Média | Três janelas mais lentas de 45 quadros |
| --- | ---: | ---: |
| SPU sem SSE4.1 | 55,0 FPS | 39,8–42,0 FPS |
| SPU com SSE4.1 | 55,9 FPS | 41,4–43,5 FPS |

São sessões diferentes. As médias incluem a captura inteira; as janelas
foram selecionadas pelo tempo médio, com distância mínima de 90 quadros
entre inícios. Elas não medem necessariamente a mesma fase exata do golpe.
O resultado favorece SSE4.1 de forma modesta; não prova causalidade nem
garante ganho em outras máquinas.

Persistem quedas na batalha, especialmente no ataque de pólen do Mothman.
Não anunciar 60 FPS estáveis, campanha inteira validada ou equivalência ao RPCS3.
Android/Odin 2 Portal ainda não tem build validada neste trabalho.

## Antes da primeira publicação

- Receber fontes reais e conferir licenças, créditos e conteúdo da coleta.
- Consolidar SDK modificado num patch reproduzível sobre revisão fixa.
- Documentar geração local PPU/SPU e dependências Windows.
- Fazer build limpa em diretório novo, preservando a instalação atual.
- Testar corredor, três ataques Mothman, seleção de ataque e save/load.
- Publicar fontes/scripts originais e instruções; não publicar executável
  que contém o código recompilado do jogo, ELF, código gerado ou assets.

## Fechamento Windows confirmado — 2026-10-12

O relatório recebido confirmou compilação incremental e fechamento por Alt+F4:
janela detectada e fechada, processo encerrado, código de saída 0 e observação
em cerca de 0,1 segundo após a janela desaparecer (resolução de amostragem do
monitor de 0,1 segundo). O marcador confirmou savedata quiescente. Não houve
espera por uma operação ativa nesse teste; esse caso foi testado apenas localmente
com o gate atômico real e APIs de host simuladas.

O log contém uma operação ListLoad2 com resultado terminal 2 do callback.
Isso não comprova salvar e reabrir/carregar com sucesso. A confirmação manual de save/load foi recebida na atualização abaixo;
o teste de compilação limpa ainda está pendente.

## Confirmação manual — 2026-10-12

LUUKASR1 confirmou salvar, reabrir o jogo e carregar o save após a correção de fechamento. Trata-se de confirmação manual do usuário, além do relatório de Alt+F4 já analisado. A compilação limpa permanece pendente.
