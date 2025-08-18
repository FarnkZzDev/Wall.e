#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/../server"

# 1) ffmpeg
if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg no encontrado. Instálalo: macOS -> brew install ffmpeg | Ubuntu -> sudo apt install ffmpeg"
  exit 1
fi

# 2) .venv
if [ ! -d ".venv" ]; then
  echo "Creando entorno virtual..."
  python3 -m venv .venv
fi
source .venv/bin/activate

# 3) deps
pip install -r requirements.txt

# 4) .env
if [ -f ".env" ]; then
  export $(grep -v '^#' .env | xargs)
else
  export HOST=0.0.0.0 PORT=8000 OLLAMA_URL=http://localhost:11434 OLLAMA_MODEL=phi3:mini USE_LOCAL_STT=1 LANG_STT=es-PE
fi

echo "Asegurate de tener Ollama corriendo y el modelo descargado:"
echo "  ollama pull $OLLAMA_MODEL"
echo "  ollama run  $OLLAMA_MODEL"
echo

# 5) run
python -m uvicorn server:app --host "$HOST" --port "$PORT"
