# Renderer nativo — etapa gráfica 0.1.14 (experimental)

O runtime Windows executa o código recompilado na CPU x86_64 e envia comandos e
shaders DirectX 12 diretamente à GPU escolhida pelo backend. A loja utilizada
para distribuir um jogo não muda essa relação com o hardware. Nenhum executável
de emulador ou firmware é usado pelo fluxo standalone.

## Implementado nesta etapa

- Configurações independentes de resolução interna e saída no launcher.
- Textura principal de cena/depth redimensionada na GPU e viewport/scissor
  remapeados a partir das coordenadas originais do guest. Os alvos offscreen do
  jogo mantêm seu significado original; classificações usam dimensões guest.
- Pipeline DirectX 12 com alvo de cena separado dos buffers de apresentação,
  estados de recursos explícitos, comandos/allocators associados às fences
  existentes e preservação do estado COMMON entre batches do renderer.
- FSR **1**, espacial: shaders EASU e RCAS oficiais da AMD, imutáveis e verificados
  por SHA-256. Nitidez configurável; render size precisa ser <= output size.
- Sem upscaler: cópia direta GPU se os tamanhos coincidirem, interpolação
  bilinear se a imagem precisar mudar de tamanho. Isso não é chamado de FSR/DLSS.
- Default: 1280×720, sem FSR, sem geração de frames. O caminho padrão mantém os
  buffers do renderer anterior. Debug overrides de framebuffer não são
  combinados com o pipeline experimental.

O launcher gráfico usa `DisgaeaD2Recomp-Graphics-Preview`. Quando encontra o
perfil standalone 0.1.13, copia somente a configuração e compila um runtime
gráfico em outra pasta, reutilizando ELF, código gerado, atualização, DLCs e
saves existentes. Não substitui o executável ou a configuração da 0.1.13.
As novas configurações são aplicadas ao abrir novamente o jogo.

## Ainda pendente: DLSS, FSR temporal e frame generation

Não há opções falsas de ativação. A configuração recusa DLSS/FG enquanto os
requisitos não forem satisfeitos. Este renderer ainda mistura cena/HUD e não
produz vetores de movimento confiáveis nem jitter/histórico de câmera.

Para a próxima etapa precisamos:

1. Identificar limites reais das passes de cena, transparência e HUD no jogo.
2. Gerar motion vectors por objeto/câmera a partir de identidade e transforms
   estáveis, inclusive sprites animados; disponibilizar depth amostravel.
3. Aplicar jitter somente à cena, com histórico/reset em cortes e mudanças de
   resolução, e validar buffers visualmente nos cenários de batalha/menu/save.
4. Integrar FSR temporal ou NVIDIA Streamline/DLSS com detecção real de suporte
   e reportar estado efetivamente ativo e falhas. FG precisa de recursos,
   sincronização/pacing e composição de HUD específicos; não duplica frames
   nem altera a velocidade da simulação de 60 Hz.

Não se promete ganho de FPS: aumentar resolução pode elevar o custo da GPU;
upscaling não resolve gargalos de CPU. Texturas originais não ganham novos
detalhes por aumentar resolução. A validação visual completa no jogo fica
pendente do teste no PC do usuário.

## Verificação

O CI testa shaders/recursos em DirectX 12 WARP sem ROM ou código de jogo: compara
dimensões reais dos alvos, pixels de saída nas bordas/centro e frames consecutivos
em 1:1, resolução interna maior, bilinear e FSR 1. Quando disponível, o debug
layer verifica mensagens de erro/corrupção. Também compila a cópia adaptada do
backend SDK auditado. WARP é usado apenas no teste; o jogo seleciona GPU hardware.

## Fontes primárias

- [AMD FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR/tree/a21ffb8f6c13233ba336352bdff293894c706575): cabeçalhos sob licença MIT em `port/graphics/vendor/fsr1`.
- [DLSS Super Resolution inputs](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md).
- [DLSS frame generation inputs](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md).
- [FSR 3 integration requirements](https://gpuopen.com/fidelityfx-super-resolution-3/).

English: this is the internal-resolution/FSR 1 stage, not a completed DLSS or
frame-generation integration. Temporal methods require trustworthy motion,
depth, camera history and HUD composition inputs that are not yet present.
