# Saves do PS3 → Windows / PS3 saves → Windows

## Português brasileiro

### Estado atual: importação experimental, ainda não validada

A transferência de um save real do **PS3 original** para este port ainda não foi validada. Save/load criados no próprio port funcionaram nos testes do mantenedor; isso não confirma compatibilidade com saves do console.

O runtime lê os arquivos do save diretamente. **O launcher 0.2.0 não descriptografa saves do PS3 e não possui um botão de importação de saves.** Exportar para USB e descriptografar são etapas diferentes: uma cópia feita pelo console pode continuar contendo arquivos cifrados. Não basta mover essa cópia para a pasta do PC para garantir que funcione.

### Antes de começar

- Mantenha o save original no PS3 e uma cópia de segurança fora da pasta usada no teste.
- Feche o jogo antes de copiar, mover ou restaurar saves no Windows.
- Faça backup da pasta `savedata` já existente no PC antes de qualquer alteração.
- O port atual usa a versão **USA / US 1.40**, com executável BLUS31313 reconhecido. Não foi validada a transferência de saves de outras regiões ou versões. Renomear uma pasta não converte região ou formato.

### Procedimento para um teste de compatibilidade

1. No PS3, tente exportar o save de Disgaea D2 para um pendrive pela **Utilidade de Dados Guardados (PS3) / Saved Data Utility (PS3)**. A cópia precisa ser permitida pelo console. Se estiver bloqueada, este guia não fornece um procedimento alternativo.
2. Confira se os **arquivos de dados do save** já estão descriptografados. A presença de `PARAM.SFO` ou `PARAM.PFD` não comprova isso. A descriptografia não é feita pelo port; obter arquivos descriptografados é um requisito separado para este teste. Se você só tem a exportação normal do PS3 e não sabe seu estado, pare aqui e peça orientação antes de importar.
3. No launcher Windows, abra **Arquivos e suporte → Abrir pasta de saves**. Na versão 0.2.0, o botão abre a pasta de usuários; entre em `00000001` e depois em `savedata`. O destino padrão completo é:

   ```text
   %LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview\hdd0\home\00000001\savedata
   ```

4. Com o jogo fechado e o backup do PC guardado, copie a **pasta completa do slot descriptografado** para dentro de `savedata`, preservando o nome original, `PARAM.SFO` e os demais arquivos. A pasta do slot deve ficar diretamente dentro de `savedata`, sem adicionar outra camada `PS3/SAVEDATA`. Não copie o pendrive inteiro.
5. Se já houver uma pasta com o mesmo nome no destino, **não sobrescreva nem misture arquivos**. Interrompa o teste e peça orientação. Não apague seu progresso para tentar importar outro save.
6. Abra o jogo e confira se o slot aparece e carrega corretamente. Se houver erro, não grave por cima do save importado. Feche o jogo e preserve a cópia original para análise.

Este procedimento pode falhar por cifragem, metadados, região, versão, conteúdo/DLC ou diferenças ainda não identificadas. Reconhecer o slot na lista não basta: carregar o progresso corretamente e depois testar um novo save/load são verificações distintas.

### Como relatar o resultado

Abra um [relato de compatibilidade ou erro](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml) com a versão do launcher, região/edição do jogo no PS3, versão da atualização, método de exportação e se os dados já estavam descriptografados. Informe se o slot apareceu, se carregou e qual mensagem foi exibida.

Não publique credenciais, chaves do console, IDs de conta nem o save completo numa issue pública. Os logs podem conter caminhos locais; confira antes de compartilhar. Um resultado só será tratado como compatibilidade confirmada depois de um teste real bem-sucedido.

## English

### Current status: experimental import, not yet verified

Transferring a save from an **original PS3 console** to this port has not been verified. Saving/loading saves created within the port worked in the maintainer's tests; this does not establish console-save compatibility.

The runtime reads save files directly. **Launcher 0.2.0 does not decrypt PS3 saves and has no save-import button.** Exporting to USB and decrypting are separate steps: a normal console export may still contain encrypted data. Moving it into the PC folder does not guarantee compatibility.

### Before starting

- Keep the original save on your PS3 and an untouched backup outside the test folder.
- Close the game before copying, moving or restoring saves on Windows.
- Back up the PC's existing `savedata` directory before making changes.
- The current port targets the **US 1.40** version with the recognized BLUS31313 executable. Other save regions or versions have not been verified. Renaming a folder does not convert its region or format.

### Compatibility test procedure

1. Try exporting your Disgaea D2 save to a USB drive through the PS3's **Saved Data Utility (PS3)**, if the console allows copying. This guide does not provide an alternative procedure for copy-protected saves.
2. Establish whether the save's **data files** are already decrypted. Having `PARAM.SFO` or `PARAM.PFD` does not prove this. The port does not perform decryption; obtaining decrypted files is a separate prerequisite. If you only have a normal console export and are unsure, stop here and ask for guidance before importing it.
3. In the Windows launcher, open **Arquivos e suporte → Abrir pasta de saves**. In 0.2.0 this opens the users directory; enter `00000001`, then `savedata`. The full default destination is:

   ```text
   %LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview\hdd0\home\00000001\savedata
   ```

4. With the game closed and your PC backup stored safely, copy the **complete decrypted save-slot folder** into `savedata`. Preserve its original folder name, `PARAM.SFO` and the remaining files. Place the slot folder directly inside `savedata`, without an extra `PS3/SAVEDATA` layer. Do not copy the entire USB drive.
5. If a folder with the same name already exists, **do not overwrite it or merge files**. Stop and ask for guidance. Do not delete your existing progress to attempt the import.
6. Launch the game and check whether the slot appears and loads correctly. If it fails, do not save over the imported slot. Close the game and retain the original backup for investigation.

Encryption, metadata, region, game version, content/DLC or other unknown differences may prevent loading. A slot appearing in the list is not sufficient proof: correctly loading progress and subsequently testing a new save/load are separate checks.

### Reporting results

Open a [compatibility or bug report](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml) with the launcher version, console game region/edition, update version, export method and whether the data was decrypted. Describe whether the slot appeared, whether it loaded and any error shown.

Do not post credentials, console keys, account IDs or a complete save in a public issue. Review local paths before sharing logs. Console-save compatibility will only be considered confirmed after a successful real-world test.
