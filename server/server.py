import random
import os, tempfile, uuid, asyncio, logging
from pathlib import Path
from dotenv import load_dotenv

from fastapi import FastAPI, UploadFile, Response
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from starlette.responses import StreamingResponse

from pydub import AudioSegment
import pyttsx3
import requests
import aiohttp

logging.basicConfig(level=logging.INFO)

# === ENV ===
load_dotenv()
HOST = os.getenv("HOST", "0.0.0.0")
PORT = int(os.getenv("PORT", "8000"))
OLLAMA_URL   = os.getenv("OLLAMA_URL", "http://localhost:11434")
OLLAMA_MODEL = os.getenv("OLLAMA_MODEL", "gemma3:1b")  
# alternativas: llama3.2:3b, llama3.1:8b, phi3:mini, gemma3:1b, deepseek-r1:8b, qwen3:8b.
USE_LOCAL_STT = os.getenv("USE_LOCAL_STT", "1") == "1"
LANG_STT = os.getenv("LANG_STT", "es-PE")
RATE_HZ = 16000

# Variable global para reproducción de audio
last_audio_id = None

# STT backends
if USE_LOCAL_STT:
    from faster_whisper import WhisperModel
    whisper_model = WhisperModel("base", compute_type="int8", device="cpu")
else:
    import speech_recognition as sr
    whisper_model = None

# === FastAPI ===
app = FastAPI(title="AgroWall.E Assistant Server", version="1.1.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"], allow_credentials=True,
    allow_methods=["*"], allow_headers=["*"],
)

# Sirve /web y / (index.html)
WEB_DIR = Path(__file__).parent.parent / "web"
app.mount("/web", StaticFiles(directory=str(WEB_DIR)), name="web")

@app.get("/", response_class=FileResponse)
def root():
    return WEB_DIR / "index.html"

# ===== Reproductor compartido (para /play y /stop) =====
playback = {
    "id": None,              # id de la última respuesta
    "wav": b"",              # bytes WAV
    "cancel": asyncio.Event()# evento para cortar streaming
}
CHUNK = 4096

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
    "Pero no inventes información falsa ni respondas más de lo que te pregunte.\n"
)

def llm_agri_ollama(question: str) -> str:
    try:
        payload = {
            "model": OLLAMA_MODEL,
            "prompt": f"{SYSTEM_PROMPT}\n\nPregunta:¿{question}?",
            "temperature": 0.6,
            "max_tokens": 100,
            "stream": False
        }
        ollama_url = f"{OLLAMA_URL}/api/generate"
        response = requests.post(ollama_url, json=payload, timeout=60)
        response.raise_for_status()
        answer = (response.json().get("response", "") or "").replace("\n", " ").strip()
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

# === API principal ===
@app.post("/qa")
async def qa(file: UploadFile):
    global last_audio_id
    logging.info("Recibiendo archivo de audio...")
    raw = await file.read()
    logging.info("Audio recibido, comenzando proceso de STT...")
    try:
        wav_in = to_wav16k(raw, file.content_type)
        logging.info("Audio convertido a formato WAV")
    except Exception:
        msg = "No pude convertir el audio. Instala FFmpeg y vuelve a intentar."
        wav_out = tts_local_wav(msg)
        # aunque sea error, preparamos playback por consistencia
        playback["id"] = str(uuid.uuid4())
        playback["wav"] = wav_out
        playback["cancel"] = asyncio.Event()
        return Response(content=wav_out, media_type="audio/wav")

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
    
    playback["id"] = str(uuid.uuid4())
    last_audio_id = playback["id"]  # Actualiza el ID

    wav_out = tts_local_wav(answer)
    logging.info("Respuesta generada en audio, enviando respuesta...")

    # Prepara el reproductor para /play y /stop
    playback["id"] = str(uuid.uuid4())
    playback["wav"] = wav_out
    playback["cancel"] = asyncio.Event()  # reinicia cancelación

    return Response(content=wav_out, media_type="audio/wav")

# === Streaming de la última respuesta ===
@app.get("/play")
async def play():
    if not playback["wav"]:
        return Response(status_code=204)

    cancel_event = playback["cancel"]

    async def streamer():
        data = playback["wav"]
        for i in range(0, len(data), CHUNK):
            if cancel_event.is_set():
                break
            yield data[i:i+CHUNK]
            await asyncio.sleep(0.005)  # deja respirar el loop para poder cortar

    return StreamingResponse(streamer(), media_type="audio/wav")

# === Sensor de Humedad ===
@app.get("/get_humidity")
async def get_humidity():
    try:
        humidity = await get_humidity_from_esp()
        
        if humidity is None:
            return {"error": "No se pudo conectar al sensor"}
        
        return await generate_humidity_response(humidity)
        
    except Exception as e:
        logging.error(f"Error en get_humidity: {str(e)}")
        return {"error": str(e)}

@app.get("/humidity_audio")
async def humidity_audio(h: int):
    try:
        if h < 20:
            message = f"Emergencia! Humedad {h}%. Suelo extremadamente seco. Riega inmediatamente."
        elif h < 50:
            message = f"Humedad {h}%. El suelo necesita riego pronto."
        elif h < 80:
            message = f"Humedad {h}%. Nivel óptimo de humedad."
        else:
            message = f"Alerta! Humedad {h}%. Suelo sobresaturado."
        
        wav_out = tts_local_wav(message)
        return Response(content=wav_out, media_type="audio/wav")
        
    except Exception as e:
        logging.error(f"Error en humidity_audio: {str(e)}")
        return Response(status_code=500)


async def get_humidity_from_esp():
    try:
        # Configuración - reemplaza con la IP de tu ESP32
        ESP32_IP = "192.168.18.42"  # Cambia por la IP local de tu ESP32
        ESP32_PORT = 80
        TIMEOUT = 5  # segundos
        
        url = f"http://{ESP32_IP}:{ESP32_PORT}/humidity"
        
        async with aiohttp.ClientSession() as session:
            try:
                async with session.get(url, timeout=TIMEOUT) as response:
                    if response.status == 200:
                        data = await response.json()
                        return int(data["humidity"])
                    else:
                        logging.error(f"Error al obtener humedad. Código: {response.status}")
                        return None
            except asyncio.TimeoutError:
                logging.error("Timeout al conectar con ESP32")
                return None
            except Exception as e:
                logging.error(f"Error en get_humidity_from_esp: {str(e)}")
                return None
    except Exception as e:
        logging.error(f"Error general en get_humidity_from_esp: {str(e)}")
        return None

# === Corte de reproducción ===
@app.post("/stop")
async def stop():
    playback["cancel"].set()
    return {"stopped": True, "id": playback["id"]}

# Nuevo endpoint para verificar audio
@app.get("/check_audio")
async def check_audio():
    return {"audio_available": (last_audio_id is not None), "audio_id": last_audio_id}

# === Arranque (si se corre con `python server.py`) ===
if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host=HOST, port=PORT)
