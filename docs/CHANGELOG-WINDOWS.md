# 1.0.52 — Lançamento oficial Windows + Android

- Primeira release oficial com **Windows e Android juntos** (APK 0.10.1 anexado). Rótulos de "prévia" removidos da versão europeia e do núcleo Android no launcher (a japonesa continua marcada como experimental).
- Android 0.10.1: idiomas English (padrão), Português, 日本語 e Español, com seletor na tela inicial.

# 1.0.51 — Prévia europeia + núcleo Android (prévia)

- **Android: aviso de troféu no estilo PlayStation, com o nome do troféu.** Cada troféu registrado faz um banner descer do topo da tela, com a taça do tipo (bronze, prata, ouro ou platina), o nome do troféu e o ícone original do jogo quando disponível. Com a trava ligada, um troféu bloqueado mostra o aviso em vermelho. Os 74 nomes vêm embutidos no app; escolhendo a ISO do jogo (tela inicial ou **Cheats → Escolher ISO do jogo ou TROPHY.TRP**) o app também pega os ícones originais. Em **Cheats**: **Testar o aviso de troféu** e **Resetar troféus**.
- **Android: menu, abas e pop-ups com o visual da tela inicial** (painel claro e botões 3D brilhantes). Atalho L3 + R3 abre o menu do jogo.
- Esta versão do Android vem no APK 0.9.3; o núcleo é o mesmo da 1.0.50.

# 1.0.50 — Prévia europeia + núcleo Android (prévia)

- **Android: aviso de troféu na tela, no estilo PlayStation.** Cada troféu registrado faz um banner descer do topo da tela por alguns segundos ("Troféu desbloqueado!"); com a trava ligada, um troféu bloqueado mostra um banner vermelho ("Troféu não registrado"). No menu do jogo → **Cheats** há o botão **Testar o aviso de troféu**. Precisa do núcleo desta versão.
- **Android: resetar troféus.** Novo botão em **Cheats → Resetar troféus** (mostra quantos estão registrados): apaga os troféus deste aparelho, sem mexer nos saves; com o jogo aberto, ele é fechado com segurança (sem gravação em andamento) e na próxima vez começa sem troféus.

# 1.0.49 — Prévia europeia + núcleo Android (prévia)

- **Versão japonesa (BLJS10206 1.30), experimental.** Suporte contribuído por DesertDoggy (PR #4): o executável japonês é mapeado para o código da 1.40 por correspondência de assinaturas, com perfil de cheats próprio. Na página **Arquivos e suporte → Versão japonesa**, escolha a pasta do disco japonês e as **três atualizações do disco (1.10, 1.20 e 1.30, todas obrigatórias)**; o launcher instala as atualizações, prepara o executável e compila o jogo no seu PC (nada do jogo é distribuído). Testado pelo autor até o primeiro mapa; o restante da campanha ainda não foi validado. Os perfis americano e europeu não mudam. No Android, o núcleo japonês compila pelo launcher, mas ainda não foi testado no aparelho.
- **Trava de troféus opcional (Android).** No menu do jogo → **Cheats**, a opção "Bloquear troféus ao usar cheats" (desligada por padrão) faz o jogo deixar de registrar novos troféus depois que você usar qualquer cheat na sessão. Com a opção desligada nada muda. Para o Windows, a opção virá junto com a janela de cheats; o núcleo já a aceita pela variável `D2_TROPHY_LOCK=1`. Precisa do núcleo desta versão.
- **Android: controles na tela** (direcional ou analógico, ✕ ○ □ △, L1/L2/R1/R2, Start/Select, analógico direito opcional e botão de menu), com opções de tamanho, opacidade e vibração na aba **Controles**; somem sozinhos quando um controle físico está conectado. **Editor de personagens** nos Cheats e correção do chão com textura trocada depois de minimizar, entrar num mapa e sair.

# 1.0.48 — Prévia europeia + núcleo Android (prévia)

- **Android: teste contra o chão errado depois de minimizar, entrar num mapa e sair.** Depois que o app volta do segundo plano, o núcleo passa a conferir todas as palavras de cada textura (antes amostrava cerca de 1024), para não reaproveitar a textura antiga do mapa no lugar da do hub. Experimental; se o chão ainda sair errado, envie captura e log. Precisa do núcleo desta versão.

# 1.0.47 — Prévia europeia + núcleo Android (prévia)

- Android: **saves e slots** (salvar o save atual num slot, importar saves do Windows/PC por pasta ou .zip, carregar, exportar e apagar slots), também **dentro do jogo** (aba Saves do menu); backup automático antes de cada JOGAR. O relógio do jogo agora para quando o app vai para segundo plano. Precisa do núcleo desta versão.

# 1.0.46 — Prévia europeia + núcleo Android (prévia)

- Android: o áudio é pausado quando o app vai para segundo plano (antes o último trecho ficava tocando em loop); voltar ao app pelo ícone retorna direto ao jogo (sem cair no launcher); "Compartilhar log" mantém as linhas importantes do meio do log. Precisa do núcleo desta versão.

# 1.0.45 — Prévia europeia + núcleo Android (prévia)

- Android: menu do jogo refeito em estilo "vidro" (translúcido, com desfoque no Android 12+), com abas **Vídeo / Filtros / Sistema**, rolagem e botão Continuar sempre visível. Tela inicial refeita no estilo do launcher do Windows (JOGAR, Escolher ISO, Configurações). Correção provável das texturas trocadas depois de minimizar e voltar.

# 1.0.44 — Prévia europeia + núcleo Android (prévia)

- Android: menu dentro do jogo (Voltar ou L3+R3) com resolução interna, contador de FPS, liberação antecipada e **Sair do jogo** (salva e fecha); filtros de imagem (nitidez, scanlines, CRT, vinheta, cores vivas, FXAA); ícone próprio do app.

# 1.0.43 — Prévia europeia + núcleo Android (prévia)

- Android: o perfilador identifica os endereços fora do jogo (função da libc ou mapeamento) e quem os chama.

# 1.0.42 — Prévia europeia + núcleo Android (prévia)

- Android: o caminho rápido de acesso à memória do jogo (anunciado na 1.0.41, que não o continha por engano) agora é aplicado pela geração do núcleo, numa cópia do SDK.

# 1.0.41 — Prévia europeia + núcleo Android (prévia)

- Runtime (Android e Linux): caminho rápido para leituras/escritas de memória do jogo (vm_read32/64, vm_write32/64) quando nenhum diagnóstico está ativo.

# 1.0.40 — Prévia europeia + núcleo Android (prévia)

- Android: perfilador de amostragem das duas threads mais ocupadas (log `[profile]` a cada 30 s) para achar o que consome a thread do jogo.

# 1.0.39 — Prévia europeia + núcleo Android (prévia)

- Android: o código traduzido do PS3 é compilado com -O2 (antes -O1) para acelerar a thread do jogo; a geração do núcleo demora mais.

# 1.0.38 — Prévia europeia + núcleo Android (prévia)

- Vulkan: vertex shaders declaram só os atributos lidos (vértices de ~3-4 slots em vez de 16).

# 1.0.37 — Prévia europeia + núcleo Android (prévia)

- Vulkan: diagnóstico das esperas do host (fences, acquire, present) no log de pacing.

# 1.0.36 — Prévia europeia + núcleo Android (prévia)

- Android: botão para ligar/desligar a liberação antecipada do quadro (comparação de desempenho e imagem).

# 1.0.35 — Prévia europeia + núcleo Android (prévia)

- Android: o jogo é liberado no flip logo após a cópia dos vértices, sobrepondo desenho/apresentação ao quadro seguinte; resolução interna refeita via buffer da superfície.

# 1.0.34 — Prévia europeia + núcleo Android (prévia)

- Android: botões de resolução interna (50–100 %) e contador de FPS no painel do app.

# 1.0.33 — Prévia europeia + núcleo Android (prévia)

- Vulkan: leitura de vértices em paralelo nos núcleos ociosos.

# 1.0.32 — Prévia europeia + núcleo Android (prévia)

- Vulkan: vértices compactos (só os atributos lidos pelo shader) e prefetch dos dados do jogo.

# 1.0.31 — Prévia europeia + núcleo Android (prévia)

- Vulkan: a leitura de vértices grava só os atributos que o shader lê.

# 1.0.30 — Prévia europeia + núcleo Android (prévia)

- Vulkan: leitura de vértices mais rápida (plano de decodificação por desenho).

# 1.0.29 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: renderizador Vulkan compilado com -O2 e processo restrito aos núcleos rápidos da CPU (mapa pesado).

# 1.0.28 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: relatório de CPU por thread e frequência dos núcleos a cada 5 s (diagnóstico).

# 1.0.27 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: diagnóstico de CPU por quadro (thread de renderização e processo) e composição dos desenhos a cada 600 quadros.

# 1.0.26 — Prévia europeia + núcleo Android (prévia)

- Vulkan: reaproveitamento de vértices em desenhos indexados, sem varredura redundante de vértices; log da composição dos desenhos.

# 1.0.25 — Prévia europeia + núcleo Android (prévia)

- Vulkan: desenhos indexados (quad = 4 vértices) e uma cópia a menos por vértice; menos custo por quadro em cenas pesadas.

# 1.0.24 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: minimizar/voltar recria a superfície Vulkan (sem tela preta); o jogo pausa em segundo plano.

# 1.0.23 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: áudio AAudio em modo callback com buffer circular + monitor de saúde a cada 5 s.

# 1.0.22 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: relatório de saúde do áudio (AAudio) a cada 5 s para diagnosticar som baixo/chiado.

# 1.0.21 — Prévia europeia + núcleo Android (prévia)

- Áudio EU: corrida do SurMixer na inicialização (fila de eventos com chave 0) corrigida.

# 1.0.20 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: áudio pelo AAudio; menos cópias de vértices por desenho.

# 1.0.19 — Prévia europeia + núcleo Android (prévia)

- Vulkan: buffer de geometria persistente e checagem vetorizada dos vértices (menos tempo por quadro em cenas pesadas).

# 1.0.18 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: cache de pipelines Vulkan salvo em disco (menos travadas ao carregar cenas novas).

# 1.0.17 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: controle (botões, direcional e analógicos) entra no jogo pelo cellPad; o app esconde o painel quando o jogo inicia.

# 1.0.16 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: o buffer da tela do jogo passa a ter o tamanho real da tela deitada; antes a imagem ficava achatada numa faixa do aplicativo.

# 1.0.15 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: a imagem sai na orientação do app (deitada) em aparelhos com a tela montada em pé; o Android faz a rotação e o aviso permanente de "subótimo" deixou de recriar a cadeia de imagens.

# 1.0.14 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: os vídeos de abertura são pulados (ainda não há decodificador H.264 no Android), para o jogo seguir até a tela de título.

# 1.0.13 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android com gráficos: backend Vulkan na tela do app (traduz os desenhos do jogo) e compilador de shaders glslang no lugar do DXC. O núcleo é gerado de novo pelo botão "Preparar núcleo Android" e baixa o glslang (pequeno) na primeira vez.

# 1.0.12 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: saves, cache e pasta de trabalho do jogo vão para o armazenamento do app (antes o jogo tentava gravar numa pasta somente leitura e parava na recuperação de saves).

# 1.0.11 — Prévia europeia + núcleo Android (prévia)

- Núcleo Android: o código só do Windows em `main.cpp` (troca de API gráfica) deixou de ser compilado no Android; cancelamento de threads sem suporte no Android virou no-op; o compilador mostra todos os erros de uma vez.

# 1.0.10 — Prévia europeia + núcleo Android (prévia)

- A compilação Android recomeça limpa quando o launcher é aberto de outra pasta temporária (o CMake recusava a pasta de build antiga).

# 1.0.9 — Prévia europeia + núcleo Android (prévia)

- O log da compilação Android agora mostra as mensagens do compilador (antes ficavam de fora).

# 1.0.8 — Prévia europeia + núcleo Android (prévia)

- Corrige "Preparar núcleo Android": usa a ferramenta do launcher atual (a pasta de preparação antiga não a tinha) e avisa quando a preparação do jogo é de uma versão incompatível.

# 1.0.7 — Prévia europeia + núcleo Android (prévia)

- Novo botão em Configurações: "Preparar núcleo Android (prévia)" compila, neste PC, o jogo já preparado para ARM64 (Odin 2). Baixa o NDK uma vez (~750 MB). Nada do jogo é distribuído.

# 1.0.6 — Prévia europeia (BLES01939)

- Corrige o botão Jogar no perfil europeu: abre o jogo já compilado em vez de pedir ISO e PKGs americanos.

# 1.0.5 — Prévia europeia (BLES01939)

- Corrige a compilação europeia: o passo SSE4.1 dos SPU não reconhecia a pasta `spu-eu`.

# 1.0.4 — Prévia europeia (BLES01939)

- Perfil europeu experimental (pasta do jogo ou ISO BLES01939 + EBOOT.elf descriptografado), separado do americano. Pré-lançamento.

# 1.0.2 — Stable / DirectX 12

- Launcher com tema claro em vidro, menu lateral cartoon (amarelo/vermelho), novo logo Win Port e carrossel de 5 capturas de gameplay. O jogo não mudou.

# 1.0.1 — Stable / DirectX 12

- Nova tela inicial do launcher: banner com capturas do jogo, título, botão JOGAR grande e atalhos em blocos. O jogo não mudou.

# 1.0.0 — Stable / DirectX 12

- Menu de opções dentro do jogo (F1): Vídeo, Filtros, Controles e Sistema; lembra a última aba; salva em `settings.ini`.
- Filtros: nitidez, scanlines, máscara CRT, vinheta, cores vivas, FXAA, contorno nos personagens e HDR.
- Captura de tela (F12), avisos estilo Steam, pausa ao perder o foco, slots de save no jogo, ícone e DPI por monitor.
- Pacote de texturas (dump em PNG e substituição) e ferramenta de ampliação (Lanczos / Real-ESRGAN experimental).
- Launcher: novo seletor de ISO em vidro (`windows/glass_picker.py`).
- Mouse como controle fica fora da 1.0 (branch `mouse-experimental`). Detalhes: `docs/RELEASE-1.0.0.md`.

# Launcher 0.1.7-preview

- Check presence and nonzero size of 18 startup files in the extracted US game
  before preparation, cached build reuse and launching. Report missing/empty paths.
- Include the missing SndPakBgm_0.pak that blocked loading on the second PC.
- Log file validation failures without changing game files, cached build configuration or saves.
- Twenty-one tests pass locally, including blocked preparation/launch and save preservation.
- Record second-PC compilation with Clang 19.1.5 and the user's successful
  run/save/load/DLC/close confirmation after restoring the audio folder.
- This is a startup presence/size check, not whole-game integrity validation.
  No runtime or graphical profile changes.

# Launcher 0.1.6-preview

- Find all complete C++ Visual Studio installations, ordered newest first,
  and select one with an available clang-cl instead of always using the latest.
- Fix the observed 2026-without-LLVM / 2022-with-LLVM detection failure on the
  second user's PC. Support standalone LLVM in its standard Program Files folder.
- Fall back when an instance lacks vcvarsall/clang-cl or cannot initialize its environment.
- Record the selected Visual Studio and resolved tool paths, or the exact detection
  failure, in preparacao.log without logging the full environment.
- Seventeen preparation/detection tests pass locally; real second-PC compilation
  with this selection fix remains pending. No runtime or profile changes.

# Launcher 0.1.5-preview

- Recover helper exit 3221226505 after a fresh success log for the same
  NPUB31321/01.00 flag package and a complete installed file inventory.
- Require PARAM.SFO and at least one known flag EDAT; reject missing/empty files,
  unexpected payloads, paths outside the content folder, stale logs and other failures.
- Keep the ELF SHA-256 gate, original return code and warning; reuse compatible builds.
- Record save/load and DLC recognition confirmed in the isolated 0.1.4 build.
- Twelve preparation tests passed on Windows CI; a real 0.1.5 retest confirmed
  automatic DLC reimport, ELF validation, existing build reuse and game exit code 0.
- No runtime, graphical profile, game asset or license changes.

# Launcher 0.1.4-preview

- Recover the observed helper exit 3221226505 only after a fresh firmware
  success log and installed modules, or an exact US 1.40 package success log
  and installed update files. Preserve the original return code and a warning.
- Keep the supported ELF SHA-256 check mandatory after package recovery.
- Reject stale success logs, other packages/versions, missing outputs, other
  failures and timeouts. DLC installation failures are not covered by this workaround.
- Record the user's successful isolated 0.1.3 compilation and two game exits
  with code 0. A subsequent fresh 0.1.4 test confirmed automatic firmware/update
  recovery, 199/199 build completion and a brief game run/exit with code 0.
  It reused installed compilers; individual DLCs and a toolchain-free PC remain untested.
- No native runtime, graphical profile or game code changes.

# Windows candidate 0.1

Unpublished development candidate based on original project commit
`340cbed43cf8427005bcfcbf537e65190161ead8` and SDK base
`a679051ef304555291de2eb3ec8a3dbf64a761a8`.

- Windows build integration, Direct3D 12 renderer and WASAPI audio.
- Texture/font upload, pitch and palette corrections.
- Depth/stencil state correction for attack selection and effects.
- Save transaction replacement/commit correction on Windows.
- Graphics SPU idle handling, constant caches and adjacent particle grouping.
- Optional SSE4.1 for existing lifted SPU helper implementations.
- Host diagnostics and lightweight frame sampling for local investigation.
- Windows close handling through savedata admission/quiescence, verified on the user’s Windows PC (exit code 0). Ends the host process after savedata completes;
  does not yet implement cooperative guest worker stop/join.
- Consolidated Windows SDK patch, separate pinned lock, Windows bootstrap,
  fresh-build command and normal launcher.

No new performance result is claimed for the close correction. Remaining
Mothman attack drops are documented in STATUS-WINDOWS.md.
Android/Odin 2 Portal support is future work.

Save/reopen/load confirmed manually by LUUKASR1 after the close correction.
A clean build from the consolidated source-only candidate is still pending.

## Launcher 0.1.8-preview

- Interface personalizada em Tk: abas Início, Vídeo, Preparação e Arquivos e suporte.
- Resolução de exibição em janela, tela cheia sem bordas e VSync no launcher.
- Preferências validadas e persistidas separadamente; opção de restaurar padrão.
- Ajuste de janela limitado ao PID iniciado e à classe ps3recomp_d3d12.
- A imagem é ampliada; resolução interna e runtime permanecem iguais. Sem nova compilação para builds compatíveis.
- Testes de persistência, geometria, ambiente, processo, GUI e Win32 sem dados do jogo. Validação real dos novos modos permanece pendente.

## Launcher 0.1.9-preview

- Logo WIN PORT fornecida pelo usuário na tela inicial, com transparência e proporção preservadas.
- Imagem quadrada fornecida pelo usuário na barra lateral e como ícone da janela, executável, instalador e atalhos.
- Paleta vermelha, violeta e laranja alinhada à arte; barra de rolagem escura.
- Configurações de vídeo, runtime, builds e saves preservados. Sem necessidade de recompilar uma build compatível.
