# Desenvolvimento 0.1.63 — abrir o executável e jogar

O pacote experimental passa a abrir o jogo diretamente quando há preparação
local e os arquivos de origem continuam disponíveis. Usa o mesmo Backend,
perfil, VFS, verificação de arquivos, compilação automática quando necessária,
configurações de vídeo, processo do jogo e encerramento existentes. Não altera
renderer, relógio, saves ou a versão pública estável.

## Como usar

1. Extraia `DisgaeaD2.exe` para uma pasta permanente e abra.
2. Na primeira execução, o assistente permite selecionar a ISO, atualização e
   DLCs e fazer a preparação existente. Não são distribuídos dados do jogo.
3. Nas próximas aberturas, uma pequena janela mostra a inicialização e some
   quando o processo do jogo começa. Ao fechar o jogo, o aplicativo termina.
4. Para abrir o launcher/configurações, mantenha **Shift** pressionado ao abrir
   o executável, ou execute `DisgaeaD2.exe --settings`.
5. Em **Arquivos e suporte**, use **Criar atalhos de Jogar e Configurações**.
   O atalho de jogar usa `--play`; o de configurações usa `--settings`.

Não mova/apague o executável depois de criar os atalhos; recrie-os se mudar
sua pasta. Os atalhos usam o ícone embutido no próprio executável. Não precisam
de administrador. A pasta da área de trabalho é resolvida pelo Windows,
incluindo redirecionamentos como OneDrive.

A primeira preparação ainda pode baixar ferramentas/dependências e compilar
os dados derivados do jogo no computador do usuário. Um único arquivo de
entrada não significa que a ISO, o runtime preparado e os saves fiquem todos
embutidos nele. Os arquivos do perfil continuam no LocalAppData. ISO movida,
ELF ausente ou configuração inválida fazem abrir o assistente. Erros durante
o início têm uma mensagem com opção de abrir configurações e são registrados
em `preparacao.log`; não apagam ou recriam saves para tentar corrigir.

A abertura direta não habilita medição, captura ou experimentos de texturas/
atributos. Para testes de desempenho, abra Configurações e selecione as opções
antes de clicar Jogar. O ajuste de espera da preview permanece como na 0.1.62.

## Validação

Testes de perfis existentes/movidos/incompletos sem alterações em saves, volta
ao assistente, ciclo Tk de início/fechamento e erro, e atalhos Win32 reais com
espaços, Unicode e apóstrofos. A função de launch existente continua fazendo as
verificações finais; a triagem inicial não autentica a ISO nem substitui essas
verificações. Não foi possível testar o jogo real no executor CI.

Teste no PC: abrir diretamente, entrar na batalha, salvar/carregar e fechar;
reabrir segurando Shift, alterar uma configuração de vídeo e abrir novamente.
Confira também os dois atalhos opcionais. A versão é experimental.

## English

Experimental 0.1.63 opens the game directly after local preparation, using the
existing backend, profile, file checks, runtime preparation, video settings and
saves. First launch or missing/moved inputs open the setup assistant. Startup
shows a small progress window, hides it when the game process starts, and exits
after the game closes. Startup errors offer to open settings and are logged.

Hold **Shift** while opening the executable, or run `DisgaeaD2.exe --settings`,
to open the launcher. Under **Files and support**, create Play and Settings
shortcuts. These target the current executable with `--play` / `--settings` and
use its embedded icon. Keep the executable in a permanent folder; recreate
shortcuts after moving it. Windows resolves the actual desktop directory.

First preparation can still download dependencies and compile game-derived
files locally. One entry executable does not embed the user's ISO, prepared
runtime or saves. No game assets are distributed. Direct launch does not enable
measurement, image capture or texture/vertex experiments. Open settings to run
those diagnostics. Public stable release and rendering behavior stay unchanged.

Tests cover prepared/missing profiles, unchanged saves, actual Tk lifecycle and
failure recovery, and real Windows shortcuts with Unicode, spaces and quotes.
CI cannot validate the actual game. Test direct launch, battle, save/load, full
exit, Shift-to-settings, a video setting change, and both optional shortcuts.
