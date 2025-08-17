#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <WebServer.h>

// Configuración WiFi
const char* ssid = "JARA 2.4G";
const char* password = "72760782jarc";

// Puerto del servidor HTTP ESP
WebServer server(80);

// Configuración del servidor
const char* serverUrl = "http://192.168.18.23:8000";

// Pines
#define I2S_BCLK 22
#define I2S_LRC 23
#define I2S_DOUT 25
#define HUMIDITY_PIN 34  // Pin del sensor FC-28

// Audio
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 2048
uint8_t audioBuffer[BUFFER_SIZE];
String currentAudioID = "";

// Humedad
unsigned long lastHumidityCheck = 0;
const long humidityInterval = 30000; // 30 segundos

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
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

bool checkForNewAudio() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(String(serverUrl) + "/check_audio");
  
  bool newAudioAvailable = false;
  if (http.GET() == HTTP_CODE_OK) {
    String payload = http.getString();
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

void playAudioFromServer() {
  HTTPClient http;
  http.begin(String(serverUrl) + "/play");
  
  if (http.GET() == HTTP_CODE_OK) {
    WiFiClient* stream = http.getStreamPtr();
    
    // Saltar header WAV (44 bytes)
    const int WAV_HEADER_SIZE = 44;
    uint8_t header[WAV_HEADER_SIZE];
    stream->readBytes(header, WAV_HEADER_SIZE);

    // Reproducir audio
    size_t bytesRead;
    while ((bytesRead = stream->readBytes(audioBuffer, BUFFER_SIZE)) > 0) {
      size_t bytesWritten = 0;
      i2s_write(I2S_NUM_0, audioBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
      delayMicroseconds(50);
    }
  }
  http.end();
}

void sendHumidityToServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Leer sensor (valor de 0 a 4095)
  int sensorValue = analogRead(HUMIDITY_PIN);
  // Convertir a porcentaje (ajustar según tu sensor)
  int humidity = map(sensorValue, 0, 4095, 0, 100);
  
  HTTPClient http;
  http.begin(String(serverUrl) + "/get_humidity");
  
  if (http.GET() == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Respuesta humedad: " + payload);
    
    // Verificar si hay nuevo audio de respuesta
    if (checkForNewAudio()) {
      playAudioFromServer();
    }
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(HUMIDITY_PIN, INPUT);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());

  setupI2S();

// Configura el servidor web
  server.on("/humidity", HTTP_GET, []() {
    int sensorValue = analogRead(HUMIDITY_PIN);
    int humidity = map(sensorValue, 0, 4095, 0, 100); // Convierte a porcentaje
    humidity = constrain(humidity, 0, 100); // Asegura que esté entre 0-100%
    
    String response = "{\"humidity\":" + String(humidity) + "}";
    server.send(200, "application/json", response);
  });

  server.begin(); // Inicia el servidor
  Serial.println("Servidor HTTP iniciado");

  IPAddress local_IP(192, 168, 18, 42);  // Cambia por la IP que quieras
  IPAddress gateway(192, 168, 18, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Error al configurar IP estática");
  }

  delay(2000);

}

void loop() {
  //Manejar las peticiones http
  server.handleClient();

  // Verificar nuevo audio cada segundo
  if (checkForNewAudio()) {
    playAudioFromServer();
  }
  
  // Verificar humedad cada 30 segundos
  if (millis() - lastHumidityCheck > humidityInterval) {
    sendHumidityToServer();
    lastHumidityCheck = millis();
  }
  
  delay(5000);
}

void stopAudioPlayback() {
  HTTPClient http;
  http.begin(String(serverUrl) + "/stop");
  http.POST("");
  http.end();
}