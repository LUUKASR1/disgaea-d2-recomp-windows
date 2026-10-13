# Aplicativo Windows (experimental)

O launcher usa sua pasta extraida e os PKGs da sua copia para preparar uma build
local. Nao contem o jogo, atualizacao, DLCs, ELF ou codigo recompilado do jogo.

1. Execute `DisgaeaD2Launcher-Setup-Windows-x64.exe` e abra o launcher pelo menu
   Iniciar. O instalador e para o usuario atual e coloca as dependencias junto
   do executavel. Como alternativa portatil, extraia **todo** o ZIP e abra
   `DisgaeaD2Launcher.exe`; mantenha `_internal` ao lado do executavel.
2. Clique em **Jogar**. Selecione sua pasta extraida (ou `PS3_GAME`) e o PKG
   **US 1.40 / BLUS31313**. Pode incluir seus PKGs de DLC. A aba **Preparação**
   tambem permite selecionar esses arquivos antes de clicar em Jogar.
3. Aguarde a primeira preparacao: o launcher baixa Git e LLVM-MinGW portateis,
   o auxiliar oficial e firmware Sony verificado, prepara o ELF, compila e inicia
   o jogo. Os downloads ficam na pasta local do aplicativo. Nao instala
   ferramentas no sistema nem altera o PATH global; nao exige Visual Studio.
4. Nas proximas sessoes, clique em **Jogar**. A build local e reutilizada.
   Firmware PUP proprio ou um `dev_flash` existente podem ser importados como
   alternativa ao download automatico. ISO e Android ainda nao estao disponiveis.

O aplicativo mantem builds, auxiliar, saves e `preparacao.log` em
`%LOCALAPPDATA%\DisgaeaD2Recomp`. Saves ficam em
`auxiliar-rpcs3\dev_hdd0\home`. Nao apague essa pasta para trocar o launcher.
Feche o jogo antes de encerrar o launcher. As pastas e PKGs originais nao sao
reescritos. Importar os PKGs altera somente o armazenamento local do aplicativo.

## Atualizar e retomar

Feche o jogo e o launcher antes de executar o instalador da nova versao. O
destino padrao e `%LOCALAPPDATA%\Programs\DisgaeaD2Launcher`. A build e os
saves continuam em `%LOCALAPPDATA%\DisgaeaD2Recomp`, uma pasta separada que
o instalador e desinstalador nao removem. Git, compiladores e o auxiliar nao
sao desinstalados com o launcher. O atalho de desktop e opcional.

Para usar a versao portatil, extraia a nova versao para outra pasta. Preserve
`%LOCALAPPDATA%\DisgaeaD2Recomp`: ela contem a build e os saves. Quando o
launcher mostra uma build pronta, clique em **Jogar** diretamente. Se precisar
reconhecer/verificar a build para preparacao, use **Jogar** sem
selecionar PKGs/firmware novamente. Essa etapa verifica as fontes do runtime e
o ELF local antes do reuso; mudancas no runtime exigem nova build, em outra pasta.
A atualizacao de uma build compativel para o launcher 0.1.7 foi confirmada pelo
usuario, sem necessidade de repetir a preparacao.

Uma compilacao interrompida apos a geracao completa pode ser retomada. O launcher
verifica hashes dos arquivos gerados e o SDK antes de continuar. Geracao parcial
permanece preservada; uma nova pasta sera usada. O contorno
`CMAKE_SUPPRESS_REGENERATION=ON` impede o ciclo de regeneracao observado no
Windows. Mudancas manuais nos CMakeLists exigem reconfiguracao explicita.

O perfil inclui `SCREEN_AS_TEX=0`, testado pelo usuario para corrigir o fundo
do titulo. Os logs das sessoes ficam em `logs/jogo-*.log`; os de preparacao
registram comando, etapa, versao e codigo de saida. A selecao do jogo e recuperada
da ultima build pronta. Os PKGs selecionados sao limpos apos importacao bem-sucedida.

Na versao 0.1.4, o codigo 3221226505 do auxiliar depois de uma instalacao
explicitamente concluida pode ser recuperado. Para firmware, exige log novo de
sucesso e os modulos instalados. Para o update US 1.40, exige log novo confirmando
o mesmo PKG, BLUS31313/01.40 e os arquivos instalados; em seguida o ELF extraido
precisa corresponder ao SHA-256 suportado. O codigo original e o aviso ficam no
log. Outros erros e timeouts continuam interrompendo a preparacao.
A recuperacao especifica de DLC foi acrescentada na versao 0.1.5, descrita abaixo. Esse contorno nao altera o runtime nem requer recompilar builds
compativeis existentes.

O reteste real da 0.1.4 em 2026-10-12 confirmou instalacao de firmware e update,
continuacao automatica, compilacao 199/199 e inicio/encerramento do jogo com
codigo 0 numa pasta nova. Reutilizou compiladores ja instalados; nao valida
instalacao em um PC sem ferramentas nem DLCs individuais. Veja STATUS-WINDOWS.md.

## Gerar o aplicativo

O workflow **Windows launcher** gera um ZIP contendo o executavel e suas
dependencias e um instalador Inno Setup. Ele testa o interpretador embutido
e a presenca das dependencias.
Tambem verifica o checksum do patch empacotado e compila um programa C minimo
com o CMake/Ninja incluidos, LLVM-MinGW portatil e descoberta do Python embutido.
A sonda verifica isolamento TLS em threads, excecoes C++, SSE4.1, XInput e
compilacao de shader D3D, e compila a biblioteca do SDK com o Git portatil.
Esses testes nao validam uma recompilacao completa nem a execucao do jogo.
O teste do instalador cobre instalacao, execucao do interpretador instalado,
reinstalacao, atalhos e desinstalacao. Fixtures de save/configuracao e um arquivo
do usuario sao conferidos por hash para verificar sua preservacao.
O teste completo com a copia US 1.40 e necessario antes de chamar uma release
de estavel. Somente Windows x64 esta implementado; ISO e Android ainda nao.

A primeira preparacao baixa compiladores portateis para a pasta privada do
aplicativo e pode demorar. Nas proximas sessoes, a build pronta e reutilizada. O desempenho e
as limitacoes atuais do runtime continuam os mesmos de STATUS-WINDOWS.md.

## Importacao de DLC no launcher 0.1.5

A recuperacao do encerramento 3221226505 agora cobre o pacote de flags
NPUB31321/01.00 observado no teste. Exige log novo confirmando o mesmo PKG,
PARAM.SFO e pelo menos um arquivo `USRDIR/Data/flag/flag########.edat`. Todos
os arquivos listados no log devem existir e ter tamanho maior que zero.
O inventario so pode conter essas flags e os metadados/imagens conhecidos;
conteudo adicional, caminhos fora da pasta local ou inventario incompleto
impedem a recuperacao. Isso nao fornece licencas nem modifica arquivos EDAT.

O retorno original e um aviso continuam no log. A verificacao SHA-256 do ELF
continua obrigatoria, e a build compativel existente e reutilizada. Pacotes de
DLC com outros formatos ou outros erros nao sao cobertos por esse contorno.
A instalacao confirmada e diferente de validar cada conteudo dentro do jogo.
O reteste da 0.1.5 em 2026-10-12 confirmou reimportacao automatica do pacote
observado, verificacao do ELF, reuso da mesma build e inicio/encerramento do
jogo com codigo 0. A instalacao inicial do pacote e seu reconhecimento no jogo
foram confirmados anteriormente na mesma pasta.

## Deteccao de ferramentas no launcher 0.1.6

Quando ha mais de uma instalacao do Visual Studio, o launcher procura instancias
com ferramentas C++, em ordem da mais recente para a mais antiga, e seleciona
uma que tenha clang-cl disponivel. Uma instancia sem LLVM nao bloqueia o uso
de outra completa. Tambem reconhece LLVM instalado em Program Files/LLVM/bin.
Falha ao inicializar uma instancia permite tentar a proxima; os arquivos de
vcvarsall e as ferramentas sao conferidos. A escolha e registrada no log.

Nao e necessario remover outra versao do Visual Studio para usar a instalacao
completa. Se a deteccao falhar, preparacao.log passa a conter a etapa e o erro
exato. O diagnostico do segundo PC demonstrou Visual Studio 2022 com C++/LLVM
e Visual Studio 2026 com C++ sem LLVM; a selecao anterior escolhia somente a
mais recente. A compilacao no segundo PC com a 0.1.6 foi confirmada: Clang 19.1.5,
199/199 etapas e codigo 0. A execucao inicialmente bloqueou por falta de
SndPakBgm_0.pak; apos restaurar a pasta SOUND, o usuario confirmou funcionamento,
save/load, DLCs e fechamento. FPS nesse PC nao foi informado.

## Conferencia da pasta do jogo no launcher 0.1.7

Antes de preparar, reconhecer uma build existente ou jogar, o launcher confere
18 arquivos usados no inicio: PARAM.SFO, EBOOT.BIN, START.dat e START_1 a
START_6.dat, ANM_HI.dat e ANM_HI_1 a ANM_HI_6.dat, NisAnmShadow.txf e
SOUND/SndPakBgm/SndPakBgm_0.pak. Arquivos ausentes ou vazios sao listados no
aviso e em preparacao.log. Restaure-os da sua copia extraida e tente novamente;
o launcher nao baixa, substitui nem modifica esses arquivos.

A verificacao e rapida: confere presenca e tamanho maior que zero, sem ler ou
hashear os grandes arquivos. Nao comprova integridade de todos os assets,
conteudo completo da campanha ou autenticidade dos arquivos. Builds compativeis
e saves continuam preservados. Essa versao nao exige recompilacao de uma build
existente cujo runtime permaneceu igual.

## Interface personalizada e vídeo — 0.1.8

As abas Início, Vídeo, Preparação e Arquivos e suporte separam as tarefas do
launcher. A aba Vídeo permite escolher Janela ou Tela cheia sem bordas,
resolução de exibição de 1280x720 até 3840x2160 e VSync. As preferências são
salvas separadamente em `launcher-settings.json` na pasta local do aplicativo.
Use Salvar configurações ou clique em Jogar para guardar as opções. Elas são
aplicadas na próxima abertura do jogo; não durante uma sessão ativa.

Em janela, tamanhos maiores que a área disponível são reduzidos para caber,
preservando a proporção 16:9. Em tela cheia sem bordas, usa o tamanho do monitor
em que a janela do jogo apareceu. Não altera o modo de vídeo do Windows.
**A resolução de exibição amplia a imagem; a resolução interna do runtime
continua original. Não é renderização nativa em 1080p/1440p/4K.**

VSync usa a opção existente D2_PRESENT_INTERVAL. O padrão permanece desligado,
como no perfil anteriormente validado; ligar pode alterar o ritmo de quadros.
Restaurar padrão retorna a 1280x720 em janela com VSync desligado. O ajuste da
janela só alcança a classe D3D12 do processo iniciado pelo launcher. Se não
conseguir encontrá-la ou ajustar sua geometria, avisa e deixa o jogo executar.

Essa versão mantém runtime, patch, arquivos do jogo e saves. Não exige
recompilação de uma build compatível. Os novos modos precisam de confirmação
no PC do usuário; testes de janela sintética não comprovam imagem/performance
no jogo real.

## Preparacao portatil 0.1.10

A troca de compilador so se aplica a novas builds. As fontes do runtime e o
patch permanecem iguais, para preservar as builds compativeis existentes.
Compilacoes interrompidas so sao retomadas com o mesmo identificador de
ferramentas; uma build pendente feita com outro compilador e preservada, e
um novo workspace e criado. O executavel usa as bibliotecas padrao do Windows,
com libc++ e winpthreads estaticos e XInput9_1_0 do sistema.

A compilacao cruzada local de uma copia 1.00 gerou o executavel completo com
essas ferramentas. Isso valida compilacao e link, nao confirma a execucao
de uma nova build US 1.40: ainda e necessario testa-la com os arquivos proprios
do usuario. Os testes publicos do CI usam somente fontes e fixtures sinteticas.

O auxiliar recebe automaticamente as DLLs de runtime C++ da Microsoft na sua
propria pasta. O pacote oficial e as DLLs sao verificados por SHA-256. Isso
evita depender de uma instalacao global de Visual C++ Redistributable para
essa etapa. A sonda nativa do CI confere o carregamento das DLLs locais.

O launcher tambem inclui o loader Vulkan compilado das fontes oficiais da
Khronos. Ele fica junto do auxiliar e permite iniciar a preparacao mesmo
sem um loader Vulkan global. A execucao nativa do jogo continua usando D3D12
e requer um driver de video adequado. Nao e instalado um SDK Vulkan.
