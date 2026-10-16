# Disgaea D2 Windows — 0.2.1 Stable / DirectX 12

## Português

Atualização da interface da versão estável 0.2.0. Projeto comunitário independente, sem afiliação com NIS, Sony ou NVIDIA.

### Novidades da 0.2.1

- Seletor **Português (Brasil) / English** abaixo da marca WIN PORT, com troca imediata enquanto o launcher está ocioso.
- Preferência de idioma salva separadamente das opções gráficas. Menus, configurações, diálogos e mensagens comuns traduzidos; logs técnicos mantêm o texto original.
- O idioma do jogo não é alterado. Esta versão mantém as fontes do runtime, o VFS, a renderização e a preparação da 0.2.0; as medições de desempenho permanecem na linha de desenvolvimento.

### Instalar e jogar

1. Feche o jogo e o launcher. Execute `DisgaeaD2Launcher-Setup-Windows-x64.exe`, ou extraia o ZIP portátil e abra `DisgaeaD2Launcher.exe`.
2. Selecione sua própria ISO USA compatível e o PKG da atualização **BLUS31313 / US 1.40**. DLCs são opcionais e exigem seus próprios arquivos e licenças.
3. Aguarde a preparação automática e clique em **Jogar** nas próximas sessões. Não é necessário instalar ferramentas de desenvolvimento manualmente; a primeira preparação precisa de internet, espaço em disco e pode demorar.

**Compatibilidade de entrada:** a imagem precisa ser legível pelo VFS; ISOs cifradas ou alteradas e outras regiões não são garantidas. O launcher valida o executável US 1.40 reconhecido. Não são distribuídos jogo, atualizações, DLCs, firmware, chaves, saves nem código recompilado do jogo.

### Recursos

- Renderização DirectX 12, áudio, save/load e importação dos pacotes fornecidos pelo usuário.
- Configurações de resolução interna e de saída, janela/tela cheia sem bordas, VSync, seleção de Hz e FSR 1 espacial.
- HUD após upscaling continua sendo uma opção experimental, desligada por padrão, com fallback nas cenas não reconhecidas.
- Capturas de desenvolvimento desativadas nesta distribuição. **Vulkan, DLSS, Frame Generation e Android não estão disponíveis.** Mais Hz não acelera a lógica nem gera quadros adicionais.

### Atualizar e preservar saves

A versão oficial reutiliza o perfil da linha gráfica já testada:
`%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview`.
O nome é mantido por compatibilidade. Instalador e portátil usam o mesmo perfil; evite abri-los ao mesmo tempo. Não apague essa pasta. Uma atualização das fontes pode gerar uma nova build local automaticamente, mantendo os saves.

Perfis antigos `DisgaeaD2Recomp` e `DisgaeaD2Recomp-Standalone` não são removidos. A adoção compatível do perfil Standalone preserva os arquivos originais; não há migração automática de todos os perfis antigos.

### Validação e limitações

O mantenedor confirmou batalha, save/load, áudio, DLCs e fechamento no PC de teste com RTX 5070. Os testes automatizados verificam componentes do launcher, preparação, renderização e instalação; não executam a campanha comercial completa. Não há garantia de 60 FPS constantes, de todas as DLCs ou de compatibilidade com todo hardware. Windows x64 e driver com DirectX 12 são necessários. O executável não possui assinatura Authenticode.

Relate problemas em [Issues](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml). Informe versão, hardware, passos para reproduzir e o log da sessão, disponível em **Arquivos e suporte → Abrir logs do jogo**. Não anexe arquivos do jogo, PKGs, chaves ou saves pessoais. Os logs podem conter caminhos locais; confira antes de publicar.

## English

Interface update to stable release 0.2.0. Select **Português (Brasil)** or **English** below the WIN PORT branding. The idle launcher switches immediately and remembers your choice separately from graphics settings. Menus, settings, dialogs and common messages are translated; technical logs retain their original wording. This does not change the game language. Runtime, VFS, renderer and game-preparation sources remain the same as 0.2.0; performance diagnostics stay in development builds. Independent community project, not affiliated with the game's rights holders.

Run the Windows x64 installer, or extract the portable ZIP and open `DisgaeaD2Launcher.exe`. Provide your own supported US ISO and **BLUS31313 US 1.40** update PKG; DLCs require your own files and licenses. The first run downloads portable tools and builds the game locally. Internet and disk space are required; preparation can take time. Encrypted/modified ISOs and other regions are not guaranteed.

Includes DirectX 12, internal/output resolution, borderless/windowed display, VSync, refresh-rate selection and spatial FSR 1. HUD-after-upscale remains experimental and off by default. Development captures are disabled. **Vulkan, DLSS, Frame Generation and Android are unavailable.** Higher display refresh does not generate extra game frames.

The installer and portable executable share `%LOCALAPPDATA%\DisgaeaD2Recomp-Graphics-Preview`, retaining the tested profile name for save compatibility. Do not delete it or run both launchers simultaneously. Source updates may rebuild the runtime automatically. Older profiles are preserved; universal automatic migration is not provided.

The maintainer confirmed battles, audio, save/load, DLC recognition and closing on the RTX 5070 test PC. Automated checks cover components and installation, not the full commercial campaign. Locked 60 FPS, every DLC and all hardware are not guaranteed. Requires Windows x64 and a DirectX 12 driver. The executable is not Authenticode-signed.

No game data, update/DLC, firmware, keys, saves or game-derived compiled code is included. Report bugs through [Issues](https://github.com/LUUKASR1/disgaea-d2-recomp-windows/issues/new?template=bug_report.yml) with version, hardware, reproduction steps and the session log. Review local paths before sharing logs; do not upload game content or keys.
