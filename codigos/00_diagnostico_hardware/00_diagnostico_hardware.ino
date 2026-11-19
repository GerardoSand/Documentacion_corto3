/*
 * ============================================================================
 * SKETCH 00: DIAGNOSTICO PRE-CORTO (Hardware + WiFi + Adafruit IO)
 * ============================================================================
 * Prueba en 10 segundos si todos los modulos y cables funcionan antes de programar:
 *  - LM35 en GPIO 34 (ADC1)
 *  - Infrarrojo DO en GPIO 32 y AO en GPIO 35 (ADC1)
 *  - Servomotor en GPIO 25 (Alimentar VCC a 5V externos + GND comun)
 *  - RFID MFRC522 en VSPI: SCK=18, MISO=19, MOSI=23, SS=5, RST=4 (3.3V ESTRICTO)
 *  - WiFi 2.4 GHz + Adafruit IO (prueba de handshake MQTT TLS)
 *
 * Comandos por Monitor Serial (115200 baudios):
 *  - TEST_SERVO : Mueve el servo 0 -> 90 -> 180 -> 90 (sin delay bloqueante)
 *  - PING_AIO   : Fuerza envio de prueba al feed "corto3-rfid"
 *  - SCAN_WIFI  : Escanea redes WiFi 2.4 GHz visibles
 *  - PAUSA      : Pausa/reanuda reporte continuo en Serial
 * ============================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <WiFi.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     "TU_WIFI_SSID"
  #define WIFI_PASS     "TU_WIFI_PASSWORD"
  #define IO_USERNAME   "TU_USUARIO_ADAFRUIT"
  #define IO_KEY        "aio_TU_LLAVE_SECRETA_AQUI"
#endif

#include "AdafruitIO_WiFi.h"

// ---------------------- PINES SEGUROS ESP32 ----------------------
constexpr uint8_t PIN_LM35      = 34; // ADC1_CH6 (solo entrada, compatible con WiFi)
constexpr uint8_t PIN_IR_DO     = 32; // Entrada digital segura
constexpr uint8_t PIN_IR_AO     = 35; // ADC1_CH7 (solo entrada)
constexpr uint8_t PIN_SERVO     = 25; // PWM seguro para servo
constexpr uint8_t PIN_RFID_SS   = 5;  // Chip Select (SDA) SPI
constexpr uint8_t PIN_RFID_RST  = 4;  // Reset RFID (deja libres 21/22 para I2C OLED)
constexpr uint8_t PIN_SPI_SCK   = 18;
constexpr uint8_t PIN_SPI_MISO  = 19;
constexpr uint8_t PIN_SPI_MOSI  = 23;

// ---------------------- OBJETOS GLOBALES -------------------------
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedEstado = io.feed("corto3-rfid");

MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo servoTest;

// ---------------------- VARIABLES DE CONTROL ---------------------
unsigned long ultimoReporteMs = 0;
unsigned long ultimoPasoServoMs = 0;
unsigned long ultimoEnvioAioMs = 0;
const unsigned long INTERVALO_REPORTE_MS = 2000;

bool reportePausado = false;
bool pruebaServoActiva = false;
int pasoServo = 0;
const int secuenciaAngulos[] = {0, 90, 180, 90};

String bufferSerial = "";

// Convierte bytes del UID RFID a cadena hexadecimal "AA:BB:CC:DD"
String formatearUID(const MFRC522::Uid &uid) {
  String out = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) out += "0";
    out += String(uid.uidByte[i], HEX);
    if (i + 1 < uid.size) out += ":";
  }
  out.toUpperCase();
  return out;
}

// Verifica si las credenciales fueron editadas
bool credencialesConfiguradas() {
  return String(WIFI_SSID).length() > 0 &&
         String(WIFI_SSID) != "TU_WIFI_SSID" &&
         String(IO_USERNAME).length() > 0 &&
         String(IO_USERNAME) != "TU_USUARIO_ADAFRUIT" &&
         String(IO_KEY) != "aio_TU_LLAVE_SECRETA_AQUI";
}

void imprimirDiagnosticoRFID() {
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("[RFID] Version chip MFRC522: 0x"));
  Serial.print(version, HEX);
  if (version == 0x00 || version == 0xFF) {
    Serial.println(F(" -> FALLO! Revisa 3.3V y cables SCK=18 MISO=19 MOSI=23 SS=5 RST=4"));
  } else if (version == 0x91 || version == 0x92 || version == 0x88) {
    Serial.println(F(" -> OK! Comunicacion SPI valida."));
  } else {
    Serial.println(F(" -> Chip detectado (version clon/alternativa)."));
  }
}

void procesarComandoSerial(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd == "TEST_SERVO") {
    pruebaServoActiva = true;
    pasoServo = 0;
    ultimoPasoServoMs = 0;
    Serial.println(F("[SERVO] Iniciando secuencia 0 -> 90 -> 180 -> 90..."));
  } else if (cmd == "PAUSA") {
    reportePausado = !reportePausado;
    Serial.println(reportePausado ? F("[SYS] Reporte pausado.") : F("[SYS] Reporte reanudado."));
  } else if (cmd == "SCAN_WIFI") {
    Serial.println(F("[WIFI] Escaneando redes 2.4 GHz..."));
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      Serial.printf("  %d) %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
    WiFi.scanDelete();
  } else if (cmd == "PING_AIO") {
    if (ultimoEnvioAioMs != 0 && millis() - ultimoEnvioAioMs < 2200) {
      Serial.println(F("[AIO] Espera >= 2.2 s entre envios (anti-throttle)."));
    } else if (io.status() >= AIO_CONNECTED) {
      ultimoEnvioAioMs = millis();
      bool ok = feedEstado->save(String("DIAGNOSTICO_OK"));
      Serial.println(ok ? F("[AIO] Dato enviado correctamente.") : F("[AIO] Error al publicar."));
    } else {
      Serial.print(F("[AIO] No conectado. Estado actual: "));
      Serial.println(io.statusText());
    }
  } else {
    Serial.println(F("Comandos: TEST_SERVO | PING_AIO | SCAN_WIFI | PAUSA"));
  }
}

void leerSerialNoBloqueante() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      procesarComandoSerial(bufferSerial);
      bufferSerial = "";
    } else if (bufferSerial.length() < 128) {
      bufferSerial += c;
    }
  }
}

void actualizarPruebaServo() {
  if (!pruebaServoActiva) return;
  unsigned long ahora = millis();
  if (ultimoPasoServoMs == 0 || ahora - ultimoPasoServoMs >= 700) {
    ultimoPasoServoMs = ahora;
    int ang = secuenciaAngulos[pasoServo];
    servoTest.write(ang);
    Serial.printf("[SERVO] Movido a %d grados\n", ang);
    pasoServo++;
    if (pasoServo >= 4) {
      pruebaServoActiva = false;
      Serial.println(F("[SERVO] Secuencia terminada."));
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\n=== DIAGNOSTICO PRE-CORTO ESP32 ==="));

  // Configurar ADC y GPIO digitales
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LM35, ADC_11db);
  analogSetPinAttenuation(PIN_IR_AO, ADC_11db);
  pinMode(PIN_IR_DO, INPUT);

  // Configurar Servo
  ESP32PWM::allocateTimer(0);
  servoTest.setPeriodHertz(50);
  servoTest.attach(PIN_SERVO, 500, 2400);
  servoTest.write(90);

  // Configurar Bus VSPI y RFID MFRC522
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  delay(50);
  imprimirDiagnosticoRFID();

  // Conectar WiFi / Adafruit IO solo si hay credenciales editadas
  if (credencialesConfiguradas()) {
    Serial.print(F("[AIO] Conectando a WiFi y Adafruit IO"));
    io.connect();
    unsigned long inicio = millis();
    while (io.status() < AIO_CONNECTED && millis() - inicio < 12000) {
      Serial.print(".");
      delay(500);
    }
    Serial.println();
    Serial.print(F("[AIO] Estado: "));
    Serial.println(io.statusText());
  } else {
    Serial.println(F("[AIO] Credenciales por defecto detectadas -> Modo diagnostico local."));
    WiFi.mode(WIFI_STA);
  }

  Serial.println(F("Escribe TEST_SERVO, SCAN_WIFI, PING_AIO o PAUSA en Serial."));
}

void loop() {
  if (credencialesConfiguradas()) {
    io.run(10, true); // fail_fast=true: mantiene sensores vivos incluso si cae la red
  }

  leerSerialNoBloqueante();
  actualizarPruebaServo();

  // Lectura instantanea de tarjeta RFID
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = formatearUID(rfid.uid);
    Serial.print(F("[RFID] Tarjeta detectada! UID = "));
    Serial.println(uid);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // Reporte periodico cada 2 segundos
  if (!reportePausado && millis() - ultimoReporteMs >= INTERVALO_REPORTE_MS) {
    ultimoReporteMs = millis();

    // Promedio de 16 muestras en milivoltios calibrados para LM35
    uint32_t sumaMv = 0;
    for (int i = 0; i < 16; i++) {
      sumaMv += analogReadMilliVolts(PIN_LM35);
    }
    float mvLM35 = sumaMv / 16.0f;
    float tempC = mvLM35 / 10.0f;

    int irDigital = digitalRead(PIN_IR_DO);
    uint32_t irMv = analogReadMilliVolts(PIN_IR_AO);

    Serial.printf("[SENSORES] LM35: %.1f mV (%.2f C) | IR DO: %s | IR AO: %u mV | WiFi: %s\n",
                  mvLM35,
                  tempC,
                  (irDigital == LOW ? "DETECTA(LOW)" : "LIBRE(HIGH)"),
                  irMv,
                  (WiFi.status() == WL_CONNECTED ? "OK" : "OFF"));
  }
}
