import os, tempfile
from pathlib import Path
from dotenv import load_dotenv

from fastapi import FastAPI, UploadFile, Response
from fastapi.responses import FileResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware

from pydub import AudioSegment
import pyttsx3
import requests
import logging
logging.basicConfig(level=logging.INFO)

# === ENV ===
load_dotenv()
HOST = os.getenv("HOST", "0.0.0.0")
PORT = int(os.getenv("PORT", "8000"))
OLLAMA_URL   = os.getenv("OLLAMA_URL", "http://localhost:11434")
OLLAMA_MODEL = os.getenv("OLLAMA_MODEL", "phi3:mini")
USE_LOCAL_STT = os.getenv("USE_LOCAL_STT", "1") == "1"
LANG_STT = os.getenv("LANG_STT", "es-PE")
RATE_HZ = 16000

# STT backends
if USE_LOCAL_STT:
    from faster_whisper import WhisperModel
    whisper_model = WhisperModel("base", compute_type="int8", device= "cpu")  # "tiny" = más rápido, "base" = mejor

else:
    import speech_recognition as sr
    whisper_model = None

# === FastAPI ===
app = FastAPI(title="AgroWall.E Assistant Server", version="1.0.0")
# Si sirves la web desde este mismo server, CORS no es necesario, pero lo dejamos flexible:
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"], allow_credentials=True,
    allow_methods=["*"], allow_headers=["*"],
)

# Sirve /web y / (index.html)
WEB_DIR = Path(__file__).parent.parent / "web"
app.mount("/web", StaticFiles(directory=str(WEB_DIR)), name="web")

@app.get("/", response_class=HTMLResponse)
def root():
    return FileResponse(WEB_DIR / "index.html")

# === Util: convertir a WAV 16 kHz mono ===
def to_wav16k(raw: bytes, content_type: str|None) -> bytes:
    suf = ".wav" if (content_type or "").find("wav")>=0 else ".webm"
    with tempfile.NamedTemporaryFile(delete=False, suffix=suf) as f:
        f.write(raw); inp = f.name
    audio = AudioSegment.from_file(inp).set_frame_rate(RATE_HZ).set_channels(1).set_sample_width(2)
    os.remove(inp)
    out = tempfile.NamedTemporaryFile(delete=False, suffix=".wav").name
    audio.export(out, format="wav")
    b = open(out,"rb").read()
    os.remove(out)
    return b

# === STT ===
def stt_local_whisper(wav_bytes: bytes, lang=LANG_STT) -> str:
    with tempfile.NamedTemporaryFile(delete=False, suffix=".wav") as f:
        f.write(wav_bytes); path = f.name
    segments, _info = whisper_model.transcribe(path, language="es")
    text = " ".join(s.text.strip() for s in segments).strip()
    os.remove(path)
    return text

def stt_google(wav_bytes: bytes, lang=LANG_STT) -> str:
    r = sr.Recognizer()
    with tempfile.NamedTemporaryFile(delete=False, suffix=".wav") as f:
        f.write(wav_bytes); path = f.name
    with sr.AudioFile(path) as source:
        audio = r.record(source)
    try:
        text = r.recognize_google(audio, language=lang)
    except Exception:
        text = ""
    os.remove(path)
    return text.strip()

def do_stt(wav_bytes: bytes) -> str:
    try:
        if USE_LOCAL_STT and whisper_model is not None:
            return stt_local_whisper(wav_bytes)
        else:
            return stt_google(wav_bytes)
    except Exception:
        return ""

# === LLM local (Ollama) ===
SYSTEM_PROMPT = (
    "Eres un asistente técnico de agricultura, Te llamas AgroWaly. Responde en español peruano, "
    "breve (2 a 3 frases), con consejos prácticos y seguros. Si no sabes, dilo y sugiere alternativa. " 
    "Pero no inventes información falsa ni respondas más de lo que te pregunte. \n"
)

# === LLM local (Ollama) con manejo de errores ===
def llm_agri_ollama(question: str) -> str:
    try:
        #logging.info(f"Enviando pregunta a Ollama: {question}")
        
        # Definir el payload para la solicitud
        payload = {
            "model": OLLAMA_MODEL,  # El modelo que estás usando
            "prompt": f"{SYSTEM_PROMPT}\n\nPregunta:¿{question}?",    # La pregunta enviada al modelo
            "temperature": 0.6,    # Controla la aleatoriedad de la respuesta
            "max_tokens": 100,     # Limita el número de tokens en la respuesta
            "stream": False        # Si no necesitas un flujo continuo de la respuesta
        }

        # URL de la API de Ollama (ajusta si es necesario)
        ollama_url = "http://localhost:11434/api/generate"

        # Enviar solicitud POST a Ollama
        response = requests.post(ollama_url, json=payload, timeout=60)
        response.raise_for_status()  # Asegúrate de que la respuesta sea exitosa

        # Procesar la respuesta de Ollama
        response_data = response.json()
        answer = response_data.get("response", "").strip()  # Extraemos la respuesta generada
        answer.replace("\n", " ").strip() #quitamos espacios de código
        # logging.info(f"Respuesta de Ollama: {answer}")
        return answer

    except requests.exceptions.RequestException as e:
        logging.error(f"Error al conectar con Ollama: {e}")
        return "Error al procesar la pregunta con Ollama."

    except Exception as e:
        logging.error(f"Error desconocido: {e}")
        return "Error desconocido."

# === TTS local ===
def tts_local_wav(text: str, rate=RATE_HZ) -> bytes:
    eng = pyttsx3.init()
    for v in eng.getProperty('voices'):
        if "es" in v.id or "spanish" in v.id.lower():
            eng.setProperty('voice', v.id); break
    tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".wav").name
    eng.save_to_file(text, tmp); eng.runAndWait()
    audio = AudioSegment.from_file(tmp).set_frame_rate(rate).set_channels(1).set_sample_width(2)
    os.remove(tmp)
    out = tempfile.NamedTemporaryFile(delete=False, suffix=".wav").name
    audio.export(out, format="wav")
    data = open(out,"rb").read()
    os.remove(out)
    return data

from fastapi.middleware.cors import CORSMiddleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"], allow_methods=["*"], allow_headers=["*"]
)

# === API principal ===
@app.post("/qa")
async def qa(file: UploadFile):
    logging.info("Recibiendo archivo de audio...")
    raw = await file.read()
    logging.info("Audio recibido, comenzando proceso de STT...")
    try:
        wav_in = to_wav16k(raw, file.content_type)
        logging.info("Audio convertido a formato WAV")
    except Exception:
        msg = "No pude convertir el audio. Instala FFmpeg y vuelve a intentar."
        return Response(content=tts_local_wav(msg), media_type="audio/wav")

    text = do_stt(wav_in)
    logging.info(f"Texto reconocido: {text}")
    if not text:
        answer = "No te entendí bien. Repite la pregunta más cerca del micrófono."
    else:
        try:
            answer = llm_agri_ollama(text) or "No estoy seguro. Intenta reformular."
            
        except Exception:
            answer = "La IA local no respondió. Asegúrate de que Ollama está en ejecución."

    logging.info(f"Respuesta de IA: {answer}")
    
    wav_out = tts_local_wav(answer)
    logging.info("Respuesta generada en audio, enviando respuesta...")
    
    return Response(content=wav_out, media_type="audio/wav")

