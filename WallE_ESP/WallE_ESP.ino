#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <BluetoothSerial.h>
#include <ESP32Servo.h>
#include <Preferences.h>

// Configuración WiFi
const char* ssid = "JARA 2.4G";
const char* password = "72760782jarc";
const char* serverUrl = "http://192.168.18.23:8000"; // Ajusta la IP

// Configuración Bluetooth
BluetoothSerial SerialBT;
Preferences preferences;

// Definición de servos
Servo servoCabeza;
Servo servoBrazoIzq;
Servo servoBrazoDer;

// Pines según tu configuración
const int pinCabeza = 21;
const int cabezaMin = 0;  // Giro máximo a la izquierda
const int cabezaMax = 100; // Giro máximo a la derecha
const int cabezaStep = 0; // Incremento por movimiento

const int pinBrazoIzq = 13;
const int pinBrazoDer = 14;

// Motores (Puente H L9110)
const int motorAI1 = 16;  // Motor izquierdo
const int motorAI2 = 17;
const int motorBI1 = 18;  // Motor derecho
const int motorBI2 = 19;

// Sensor de proximidad (HC-SR04)
const int trigPin = 26;
const int echoPin = 27;

// Pines I2S para MAX98357A
#define I2S_BCLK 22
#define I2S_LRC 23
#define I2S_DOUT 25

// Audio
#define SAMPLE_RATE 16000   // Must match your server's output
#define BUFFER_SIZE 2048
uint8_t audioBuffer[BUFFER_SIZE];
String currentAudioID = "";

// Variables de control
int servoCabezaPos = 0;  // Posición inicial
int servoBrazoIzqPos = 180;
int servoBrazoDerPos = 0;
int pad_x = 0, pad_y = 0;
unsigned long last_time = 0;
const int update_interval = 100;

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

float medirDistancia() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH);
  return duration * 0.034 / 2; // Convertir a cm
}

void controlMotores(int x, int y) {
  // Convertir coordenadas (0-100) a (-100 a 100)
  int ejeX = map(x, 0, 100, -100, 100);
  int ejeY = map(y, 0, 100, -100, 100);

  // Zona muerta para evitar movimientos mínimos
  if(abs(ejeX) < 15 && abs(ejeY) < 15) {
    moverMotores(0, 0);
    return;
  }

  // Control direccional mejorado
  int velocidad = map(abs(ejeY), 0, 100, 0, 255);
  int giro = map(abs(ejeX), 0, 100, 0, 255);

  if(ejeY > 0) { // Adelante
    if(ejeX > 20) { // Diagonal derecha
      moverMotores(velocidad, velocidad/2);
    } 
    else if(ejeX < -20) { // Diagonal izquierda
      moverMotores(velocidad/2, velocidad);
    } 
    else { // Recto
      moverMotores(velocidad, velocidad);
    }
  } 
  else { // Atrás
    if(ejeX > 20) { // Diagonal derecha
      moverMotores(-velocidad/2, -velocidad);
    } 
    else if(ejeX < -20) { // Diagonal izquierda
      moverMotores(-velocidad, -velocidad/2);
    } 
    else { // Recto
      moverMotores(-velocidad, -velocidad);
    }
  }
}

void moverMotores(int izquierda, int derecha) {
  izquierda = constrain(izquierda, -255, 255);
  derecha = constrain(derecha, -255, 255);

  // Motor izquierdo
  if (izquierda >= 0) { // Adelante
    analogWrite(motorAI1, izquierda);
    digitalWrite(motorAI2, LOW);
  } else {              // Atrás
    analogWrite(motorAI1, -izquierda);
    digitalWrite(motorAI2, HIGH);
  }

  // Motor derecho
  if (derecha >= 0) { // Adelante
    analogWrite(motorBI1, derecha);
    digitalWrite(motorBI2, LOW);
  } else {            // Atrás
    analogWrite(motorBI1, -derecha);
    digitalWrite(motorBI2, HIGH);
  }
}

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

  // 3. Configurar Bluetooth
  SerialBT.begin("ESP32_Robot");

  // Inicializar Preferences
  preferences.begin("robot-config", false);  // false para lectura/escritura

  // Cargar posiciones guardadas (con valores por defecto si no existen)
  servoCabezaPos = preferences.getInt("cabeza", 0);
  servoBrazoIzqPos = preferences.getInt("brazoIzq", 180);
  servoBrazoDerPos = preferences.getInt("brazoDer", 0);

  // Inicializar servos con movimiento suave
  servoCabeza.attach(pinCabeza, 500, 2400);
  servoCabeza.write(servoCabezaPos);
  servoBrazoIzq.attach(pinBrazoIzq, 500, 2400);
  servoBrazoIzq.write(servoBrazoIzqPos);
  servoBrazoDer.attach(pinBrazoDer, 500, 2400);
  servoBrazoDer.write(servoBrazoDerPos);

  // Inicializar motores
  pinMode(motorAI1, OUTPUT);
  pinMode(motorAI2, OUTPUT);
  pinMode(motorBI1, OUTPUT);
  pinMode(motorBI2, OUTPUT);

  digitalWrite(motorAI1, LOW);
  digitalWrite(motorAI2, LOW);
  digitalWrite(motorBI1, LOW);
  digitalWrite(motorBI2, LOW);

  // Inicializar sensor de proximidad
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Configurar panel del gamepad
  SerialBT.println("*.kwl");
  SerialBT.println("clear_panel()");
  SerialBT.println("set_grid_size(13,5)");
  
  // Sliders para servos
  SerialBT.println("add_slider(1,0,3,0,180,90,C,A)");  // Cabeza
  SerialBT.println("add_slider(4,0,3,0,180,90,L,B)");  // Brazo izquierdo
  SerialBT.println("add_slider(7,0,3,0,180,0,R,C)");  // Brazo derecho
  
  // Pad para control de motores
  SerialBT.println("add_free_pad(10,1,0,100,0,0,X,Y)"); 
  
  // Botón proximidad
  SerialBT.println("add_button(4,4,2,P,p)");  // Botón proximidad
  
  SerialBT.println("set_panel_notes(-,,,)");
  SerialBT.println("run()");
  SerialBT.println("*");
}

void loop() {
  // Procesar datos del gamepad
  if (SerialBT.available()) {
    char data_in = SerialBT.read();

    if(data_in == 'L') {
      servoCabezaPos = SerialBT.parseInt();
      servoCabeza.write(servoCabezaPos);
      preferences.putInt("cabeza", servoCabezaPos);
    }

    if(data_in == 'R') {
      servoCabezaPos = SerialBT.parseInt();
      servoCabeza.write(servoCabezaPos);
      preferences.putInt("cabeza", servoCabezaPos);
    }
   
    if(data_in == 'B') {
      servoBrazoIzqPos = 180 - SerialBT.parseInt();
      servoBrazoIzq.write(servoBrazoIzqPos);
      preferences.putInt("brazoIzq", servoBrazoIzqPos);
    }
    
    // Slider brazo derecho
    if(data_in == 'C') {
      servoBrazoDerPos = SerialBT.parseInt();
      servoBrazoDer.write(servoBrazoDerPos);
      preferences.putInt("brazoDer", servoBrazoDerPos);
    }
    // Pad de control
    if(data_in == 'X') pad_x = SerialBT.parseInt();
    if(data_in == 'Y') pad_y = SerialBT.parseInt();
    controlMotores(pad_x, pad_y);
    
    // Botón proximidad
    if(data_in == 'P') {
      float distancia = medirDistancia();
      SerialBT.print("Dist: ");
      SerialBT.print(distancia);
      SerialBT.println(" cm");
    }
  }

  // Verificar nuevo audio cada 5 segundos
  static unsigned long lastAudioCheck = 0;
  if (millis() - lastAudioCheck > 5000) {
    lastAudioCheck = millis();
    if (checkForNewAudio()) {
      playAudioFromServer();
    }
  }

  // Actualización periódica
  unsigned long t = millis();
  if ((t - last_time) > update_interval) {
    last_time = t;
  }
}