# Aplicativo Windows (experimental)

O launcher usa sua pasta extraida e os PKGs da sua copia para preparar uma build
local. Nao contem o jogo, atualizacao, DLCs, ELF ou codigo recompilado do jogo.

1. Extraia **todo** o ZIP do launcher para uma pasta. Abra `DisgaeaD2Launcher.exe`.
   A pasta `_internal` precisa permanecer ao lado do executavel.
2. Na primeira vez, clique em **Instalar ferramentas**, caso nao tenha Git e
   Visual Studio com C++, SDK Windows e LLVM. Essa instalacao pode ocupar varios
   GB e apresentar solicitacoes do instalador oficial. Reinicie o launcher.
3. Se o auxiliar ainda nao tiver firmware, selecione seu `PS3UPDAT.PUP`, obtido
   pela pagina oficial da Sony indicada no aplicativo, ou importe a pasta do
   seu RPCS3 com `dev_flash` instalado. O launcher instala/copia somente no
   auxiliar local. Firmware nao faz parte do ZIP distribuido.
4. Selecione a pasta do jogo (ou `PS3_GAME`) e o PKG **US 1.40 / BLUS31313**.
   Pode selecionar tambem seus PKGs de DLC. Licencas RAP/EDAT nao sao fornecidas;
   conteudo que exija licenca continua precisando da sua licenca local.
5. Clique em **Preparar meu jogo**. E necessario acesso a internet na primeira
   preparacao, para obter o auxiliar RPCS3 oficial e o SDK de recompilacao.
   O RPCS3 instala pacotes e prepara o ELF; o jogo roda na build nativa depois.
6. Quando a preparacao terminar, clique em **Jogar**. A build fica armazenada e
   e reutilizada; nao precisa recompilar em cada sessao.

O aplicativo mantem builds, auxiliar, saves e `preparacao.log` em
`%LOCALAPPDATA%\DisgaeaD2Recomp`. Saves ficam em
`auxiliar-rpcs3\dev_hdd0\home`. Nao apague essa pasta para trocar o launcher.
Feche o jogo antes de encerrar o launcher. As pastas e PKGs originais nao sao
reescritos. Importar os PKGs altera somente o armazenamento local do aplicativo.

## Atualizar e retomar

Extraia a nova versao do launcher para outra pasta. Preserve
`%LOCALAPPDATA%\DisgaeaD2Recomp`: ela contem a build e os saves. Clique em
**Preparar meu jogo**, sem selecionar PKGs/firmware novamente, para reconhecer
a build existente. As fontes do runtime e o ELF local sao verificados antes do
reuso; mudancas no runtime exigem uma nova build, em outra pasta.

Uma compilacao interrompida apos a geracao completa pode ser retomada. O launcher
verifica hashes dos arquivos gerados e o SDK antes de continuar. Geracao parcial
permanece preservada; uma nova pasta sera usada. O contorno
`CMAKE_SUPPRESS_REGENERATION=ON` impede o ciclo de regeneracao observado no
Windows. Mudancas manuais nos CMakeLists exigem reconfiguracao explicita.

O perfil inclui `SCREEN_AS_TEX=0`, testado pelo usuario para corrigir o fundo
do titulo. Os logs das sessoes ficam em `logs/jogo-*.log`; os de preparacao
registram comando, etapa, versao e codigo de saida. A selecao do jogo e recuperada
da ultima build pronta. Os PKGs selecionados sao limpos apos importacao bem-sucedida.

## Gerar o aplicativo

O workflow **Windows launcher** gera um ZIP contendo o executavel e suas
dependencias. Ele testa o interpretador embutido e a presenca das dependencias.
Tambem verifica o checksum do patch empacotado e compila um programa C minimo
com o CMake/Ninja incluidos, clang-cl local e descoberta do Python embutido.
Esses testes nao validam uma recompilacao completa nem a execucao do jogo.
O teste completo com a copia US 1.40 e necessario antes de chamar uma release
de estavel. Somente Windows x64 esta implementado; ISO e Android ainda nao.

A primeira preparacao usa compiladores locais: este e um assistente que instala
e coordena essas ferramentas, nao uma recompilacao instantanea. O desempenho e
as limitacoes atuais do runtime continuam os mesmos de STATUS-WINDOWS.md.
