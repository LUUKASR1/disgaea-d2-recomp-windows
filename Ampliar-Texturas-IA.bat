@echo off
cd /d "%~dp0"
rem Amplia as texturas salvas com IA (Real-ESRGAN, modelo anime). Na primeira vez baixa o programa (~45 MB, github.com/xinntao/Real-ESRGAN)
rem para %LOCALAPPDATA%\DisgaeaD2Recomp\ferramentas\realesrgan. Precisa de placa de video com Vulkan. Salva lote a lote: pode parar e rodar de novo.
rem Opcoes:  Ampliar-Texturas-IA.bat --scale 4   (padrao 2)
python -m pip install --quiet pillow
python tools\d2_texture_upscale.py --baixar-realesrgan %*
echo.
pause
