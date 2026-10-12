@echo off
setlocal
cd /d "%~dp0" || exit /b 1
python "%~dp0iniciar_build.py"
pause
