@echo off
setlocal
cd /d "%~dp0..\server"

REM 1) FFmpeg: si no lo tienes, instala con winget (necesita Windows 10/11)
where ffmpeg >nul 2>nul || (
  echo Instalando FFmpeg...
  winget install -e --id Gyan.FFmpeg || echo Instala FFmpeg manualmente si falla winget
)

REM 2) Python venv
if not exist .venv (
  echo Creando entorno virtual...
  py -3 -m venv .venv
)
call .venv\Scripts\activate

REM 3) Dependencias
pip install -r requirements.txt

REM 4) Cargar variables desde .env (opcionales)
if exist .env (
  for /f "usebackq delims=" %%a in (".env") do set %%a
)

REM 5) Aviso sobre Ollama
echo Asegurate de tener Ollama ejecutandose y el modelo descargado:
echo    ollama pull %OLLAMA_MODEL%
echo    ollama run %OLLAMA_MODEL%
echo.

REM 6) Ejecutar servidor
python -m uvicorn server:app --host %HOST% --port %PORT%
