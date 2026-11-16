@echo off
cd /d "%~dp0"
rem Amplia as texturas salvas em texture-pack\dump e grava o pacote em texture-pack (veja tools\d2_texture_upscale.py).
rem Exemplos:  Ampliar-Texturas.bat --scale 4   |   Ampliar-Texturas.bat --realesrgan C:\\ferramentas\\realesrgan-ncnn-vulkan.exe
python -m pip install --quiet pillow
python tools\d2_texture_upscale.py %*
echo.
pause
