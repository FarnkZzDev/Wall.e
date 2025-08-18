# AgroWall.E – Asistente Agro con ESP32 + Python

## Requisitos previos
- [Python](https://www.python.org/)  
- [Visual Studio Code](https://code.visualstudio.com/)  
- [FFmpeg](https://www.gyan.dev/ffmpeg/builds/)  
- [Ollama](https://ollama.com/)  
- [ngrok](https://ngrok.com/download)  

---

## Instalación paso a paso

### Paso 1: Instalar FFmpeg
1. Descarga desde [FFmpeg Builds](https://www.gyan.dev/ffmpeg/builds/) el archivo `ffmpeg-git-full.7z`.  
2. Extrae en una carpeta ("C:\ffmpeg\").  
3. Agrega la ruta `bin` al **Path** de las variables de entorno.

---

### Paso 2: Instalar Ollama
1. Descarga el instalador desde [Ollama](https://ollama.com/).  
   > Instalar el modelo gemma3:1b (pero cualquier otro funciona, habría que cambiar el modelo en el archivo .venv y en server.py). 
   > para instalarlo hay que correr en Powershell o CMD: `ollama run gemma3:1b`
2. Asegúrate de que `ollama` esté en el Path.  
3. Para probar un modelo:  
   ```bash
   ollama run phi3:mini
   ```

---

### Paso 3: Instalar ngrok (para conexión segura)
1. Descarga desde [ngrok](https://ngrok.com/download).  
2. Descomprime y agrega tu token de autenticación:  
   ```bash
   ngrok config add-authtoken TU_TOKEN
   ```
   Obtén tu token aquí: [ngrok dashboard](https://dashboard.ngrok.com/get-started/your-authtoken).
3. Para exponer el servidor en el puerto 8000 (Hacer al final luego de la ejecución):  
   ```bash
   ngrok http 8000
   ```

---

## Ejecuta el script 
- Hay un archivo `.bat` incluido en la carpeta scripts para automatizar pasos iniciales.

 ### Nota
 - Por algún motivo suele dar pequeños problemas entonces hay que borrar la carpeta `.venv` que se creó en el directorio principal
---

### Paso 4: Configurar entorno virtual en Python
```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
```

### Paso 5: Instalar dependencias [asegurarse de estar en un entorno virtual (.venv)]
```powershell
pip install -r requirements.txt
```
Si se necesitacen hacer manualmente:
```
pip install python-multipart
pip install pytz 
pip install faster-whisper
pip install SpeechRecognition
pip install aiohttp
```

---

## Ejecución del servidor
- Asegurarse de que la computadora que carga el servidor, y el ESP estén en la misma red, además de estar en un entorno virtual (`.venv`)

Iniciar con **FastAPI + Uvicorn**:  
```powershell
.\.venv\Scripts\python.exe -m uvicorn server.server:app --host 0.0.0.0 --port 8000
```

Exponer con **ngrok** :  
```bash
ngrok http 8000
```

Copia el enlace **HTTPS** que da ngrok para acceder desde el celular. (cambia cada vez que se detiene)  

---

