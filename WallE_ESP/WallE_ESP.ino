#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>

// Configuración WiFi
const char* ssid = "SLIPKNOT";
const char* password = "L46063432*";
const char* serverUrl = "http://192.168.1.31:8000"; // Ajusta la IP


// Pines I2S para MAX98357A
#define I2S_BCLK 22
#define I2S_LRC 23
#define I2S_DOUT 25

// Audio
#define SAMPLE_RATE 16000   // Must match your server's output
#define BUFFER_SIZE 2048
uint8_t audioBuffer[BUFFER_SIZE];
String currentAudioID = "";

// ========= SETUP I2S ========= //
void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // Mono
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = true,       // Mejor calidad de clock
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

// ========= VERIFICAR NUEVO AUDIO ========= //
bool checkForNewAudio() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(String(serverUrl) + "/check_audio");
  
  bool newAudioAvailable = false;
  if (http.GET() == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // Parsear JSON manualmente (simple)
    int idIndex = payload.indexOf("\"audio_id\":\"");
    if (idIndex > 0) {
      String newID = payload.substring(idIndex + 12, payload.indexOf("\"", idIndex + 13));
      
      if (newID != "null" && newID != currentAudioID) {
        currentAudioID = newID;
        newAudioAvailable = true;
        Serial.println("[Audio] Nuevo audio disponible: " + currentAudioID);
      }
    }
  }
  http.end();
  return newAudioAvailable;
}

// ========= REPRODUCIR AUDIO ========= //
void playAudioFromServer() {
  HTTPClient http;
  http.begin(String(serverUrl) + "/play");
  
  if (http.GET() == HTTP_CODE_OK) {
    WiFiClient* stream = http.getStreamPtr();
    
    // 1. Saltar encabezado WAV (44 bytes)
    const int WAV_HEADER_SIZE = 44;
    uint8_t header[WAV_HEADER_SIZE];
    stream->readBytes(header, WAV_HEADER_SIZE);

    // 2. Reproducir datos de audio
    Serial.println("[Audio] Reproduciendo...");
    size_t bytesRead;
    while ((bytesRead = stream->readBytes(audioBuffer, BUFFER_SIZE)) > 0) {
      size_t bytesWritten = 0;
      
      // Enviar a I2S con timing controlado
      i2s_write(I2S_NUM_0, audioBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
      
      // Pequeña pausa para evitar saturación
      delayMicroseconds(100);
    }
    Serial.println("[Audio] Reproducción completada");
  } else {
    Serial.println("[Error] Falló la descarga del audio");
  }
  http.end();
}

// ========= SETUP Y LOOP ========= //
void setup() {
  Serial.begin(115200);
  
  // 1. Conectar WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());

  // 2. Inicializar I2S
  setupI2S();
  Serial.println("I2S configurado a " + String(SAMPLE_RATE) + "Hz");
}

void loop() {
  // Verificar nuevo audio cada segundo
  if (checkForNewAudio()) {
    playAudioFromServer();
  }
  delay(5000);
}