# Agro Assistant - Servidor Local + Web de Grabación

## Pasos para iniciar

### 1. Requisitos
- Python 3.9 o superior
- FFmpeg instalado
- (Opcional) Ollama para IA local

### 2. Preparar entorno
Windows:
```bat
scripts\start_windows.bat
```
### 3. Acceder desde el móvil
En el navegador del celular:
```
http://<IP_DE_TU_PC>:8000
```

Esto mostrará la interfaz de grabación.

### 4. Variables de entorno
Abrir `server/.env` y ajusta valores si se desea.

-------------------------------------------

# Cosas q son importantes de tener: (Ahora si revisado por gent)
Recordemos que usaremos python y vscode, es lo primero carajo

# Lo primero:

## Debemos asegurarnos de tener el FFmpeg https://www.gyan.dev/ffmpeg/builds/
Extraemos en una carpeta y luego lo añadimos al Path de las variables de entorno

## Ahora instalamos el OLLAMA https://ollama.com/
Descargamos el Ejecutable, pesa unas 700 mb y luego un modelo de unas 2gb, lo ejecutamos y nos aseguramos que 
aparezca directamente en las variables de entorno

Aqui hay un modelo pa instalar (correr en el shell): ollama run phi3:mini

## Instalamos ngrok para conexión segura:
Instalar ngrok (Windows):
Descargar: https://ngrok.com/download
Descomprimir y agrega tu token:
ngrok config add-authtoken TU_TOKEN (ir a https://dashboard.ngrok.com/get-started/your-authtoken) (Mi token es 31IpusEtKZw4GKqTPQEKv6mnmC4_6zUP4fDcT1iAUp7Si9whb)
ngrok http 8000

# luego:
Ejecutamos el .bat, nos ayudará con un par de cosas

## Si es que nos da problemas la primera vez, borramos el .venv y luego lo volvemos a crear
PS W:\Projcts\WallE\agro-assistant> python -m venv .venv
PS W:\Projcts\WallE\agro-assistant> .\.venv\Scripts\Activate.ps1

## Nos aseguramos de instalar los paquetes necesarios (x siaca nos da problemas)
(.venv) PS W:\Projcts\WallE\agro-assistant> pip install -r requirements.txt
(.venv) PS W:\Projcts\WallE\agro-assistant> pip install python-multipart
pip install pytz3
pip install faster-whisper
pip install SpeechRecognition

# Con esto lo vamos a correr
(.venv) PS W:\Projcts\WallE\agro-assistant> .\.venv\Scripts\python.exe -m uvicorn server.server:app --host 0.0.0.0 --port 8000

## Para que se conecte de manera segura
ngrok http 8000

Copiar el link que da para acceder a este desde el celular :)