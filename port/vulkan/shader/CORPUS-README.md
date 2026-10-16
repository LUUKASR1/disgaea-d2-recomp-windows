# Verificador de shaders capturados no seu jogo

O launcher experimental 0.1.46 oferece **Shaders — preparar Vulkan**. Habilite a captura antes de clicar em Jogar. A coleta é automática durante a primeira compilação de cada shader pelo DirectX 12, sem F8 e sem imagens. Visite menus, Castle Hallway e uma batalha, execute golpes e feche o jogo. O ZIP da sessão aparece em Downloads (ou na pasta de relatórios do launcher se Downloads não existir).

O ZIP contém `shaders/*.hlsl`, amostras RSX `*.hlsl.rsx`, metadados `*.hlsl.json` e `corpus.json`. Cada arquivo representa a primeira compilação observada daquela fonte HLSL. Não é um dump de todos os desenhos/constantes/texturas e não garante cobertura completa do jogo. Limites: 256 fontes distintas, 32 MiB de payload, 256 KiB por HLSL e 64 KiB por amostra RSX. Desative a captura para jogar normalmente.

Para analisar localmente, extraia o ZIP, abra PowerShell na pasta do verificador e execute (substitua o caminho):

```powershell
.\DisgaeaD2ShaderCorpus.exe --input "C:\caminho\da-sessao\shaders" --output "corpus-check"
```

O executável inclui DXC; não precisa de GPU Vulkan, Vulkan SDK ou compilador instalado. O relatório `corpus-check/corpus-vulkan-report.json` separa **falha de compilação** de **shader compilado com recursos fora do ABI do renderer**. Gera SPIR-V apenas para módulos compilados. Saídas: 0 = compilados sem lacunas detectadas no ABI individual; 1 = erro/falha de compilação; 2 = compilados com lacunas no ABI. Nem mesmo saída 0 prova renderização correta: pares VS/FP, estados de desenho, recursos e equivalência de pixels ainda precisam ser testados.

O teste `--self-test` usa cinco fontes próprias, incluindo uma inválida, uma incompatível com o ABI e uma com saídas MRT não contíguas, para verificar que o processamento continua e os resultados são classificados corretamente. Não utiliza shaders do jogo. Comparação automatizada de pixels com RPCS3 não está implementada. O RPCS3 segue como referência técnica de semântica RSX; nenhum código GPL foi incorporado.

O relatório lista `fragment_output_locations` e `minimum_color_targets`. O renderer aceita 1–4 alvos RGBA8 offscreen, com profundidade compartilhada; MRT requer `independentBlend`. Um shader que declara saída 2 precisa de pelo menos três alvos vinculados. A apresentação na janela ainda usa somente um alvo. Essa compatibilidade individual não garante que os alvos, formatos e estados reais do jogo tenham sido reproduzidos.

As fontes e amostras capturadas permanecem no computador do usuário e não fazem parte dos artefatos públicos. As licenças do compilador acompanham o pacote.

## English

Select **Shaders — preparar Vulkan** in launcher 0.1.46 and enable capture before starting the game. Sources are exported automatically after successful D3DCompile calls; no F8 or image capture. Visit menus, hallway and battle, then close the game and send the generated capture ZIP. Run `DisgaeaD2ShaderCorpus.exe --input "path\to\shaders" --output corpus-check` to compile the captured HLSL with embedded DXC. Compilation, individual renderer ABI compatibility, shader-pair linking and pixel equivalence are distinct checks. This tool covers only the first two; it does not prove playable Vulkan or compare rendered pixels with RPCS3. Game sources/bytecode are never bundled in public artifacts.
