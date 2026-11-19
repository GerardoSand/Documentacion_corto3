/*
 * ============================================================================
 * SKETCH 03: SERVOMOTOR DINAMICO NO BLOQUEANTE + WIFI + ADAFRUIT IO
 * ============================================================================
 * Hardware:
 *  - Servomotor SG90/MG90S/MG995:
 *      * Cable Naranja/Amarillo (Senal) -> GPIO 25
 *      * Cable Rojo (VCC)               -> 5V EXTERNOS (o VIN solo 1 SG90)
 *      * Cable Cafe/Negro (GND)         -> GND COMUN con ESP32
 *
 * Feeds en Adafruit IO:
 *  - "corto3-servo"     (ESP32 -> Dashboard): Angulo actual real (0..180)
 *  - "corto3-servo-cmd" (Dashboard Slider -> ESP32): Angulo objetivo dinamico
 *  - "corto3-modo"      (Dashboard Text/Toggle -> ESP32): "MANUAL", "BARRIDO"
 *
 * Comandos dinamicos por Serial (115200):
 *  - ANGULO=120         -> Mueve suavemente al angulo deseado
 *  - MODO=MANUAL|BARRIDO
 *  - VEL=15             -> Milisegundos por grado (menor = mas rapido)
 *  - LIMITES=10,170     -> Fija angulo minimo y maximo de proteccion mecanica
 *  - PULSO=500,2400     -> Recalibra anchos de pulso PWM en microsegundos (500..2500)
 *  - PIN=13             -> Cambia el pin PWM del servo en vivo (ej: 25 o 13)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Servo.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     "TU_WIFI_SSID"
  #define WIFI_PASS     "TU_WIFI_PASSWORD"
  #define IO_USERNAME   "TU_USUARIO_ADAFRUIT"
  #define IO_KEY        "aio_TU_LLAVE_SECRETA_AQUI"
#endif

#include "AdafruitIO_WiFi.h"

uint8_t pinServo = 25; // Dinamico (por defecto GPIO 25; soporta PIN=13, 25, etc.)

Servo miServo;

// ---------------------- PARAMETROS DINAMICOS -------------------
int anguloMinimo      = 0;
int anguloMaximo      = 180;
int pulsoMinUs        = 500;
int pulsoMaxUs        = 2400;
int anguloActual      = 90;
int anguloObjetivo    = 90;
unsigned long msPorGrado = 12; // Control de velocidad suave sin delay()

bool modoBarrido      = false;
bool aioSincronizado  = false;
int direccionBarrido  = 1;

// ---------------------- CONTROL DE TIEMPO ----------------------
unsigned long ultimoPasoServoMs = 0;
unsigned long ultimoEnvioMs     = 0;
int ultimoAnguloReportado       = -1;
String bufferSerial             = "";

// ---------------------- ADAFRUIT IO ----------------------------
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedServoPos = io.feed("corto3-servo");
AdafruitIO_Feed *feedServoCmd = io.feed("corto3-servo-cmd");
AdafruitIO_Feed *feedModo     = io.feed("corto3-modo");

bool esPinServoSeguro(int gpio) {
  return (gpio == 13 || gpio == 14 || gpio == 16 || gpio == 17 ||
          gpio == 18 || gpio == 19 || gpio == 21 || gpio == 22 ||
          gpio == 23 || gpio == 25 || gpio == 26 || gpio == 27 ||
          gpio == 32 || gpio == 33);
}

bool parseIntSeguro(const String &s, long &salida) {
  String t = s;
  t.trim();
  if (t.length() == 0) return false;
  char *endptr = nullptr;
  long val = strtol(t.c_str(), &endptr, 10);
  if (endptr == t.c_str() || *endptr != '\0') return false;
  salida = val;
  return true;
}

bool parseFloatSeguro(const String &s, float &salida) {
  String t = s;
  t.trim();
  if (t.length() == 0) return false;
  char *endptr = nullptr;
  float val = strtof(t.c_str(), &endptr);
  if (endptr == t.c_str() || *endptr != '\0') return false;
  salida = val;
  return true;
}

void fijarAnguloObjetivo(int nuevo) {
  anguloObjetivo = constrain(nuevo, anguloMinimo, anguloMaximo);
  Serial.printf("[SERVO] Objetivo = %d grados (Limites %d..%d)\n",
                anguloObjetivo, anguloMinimo, anguloMaximo);
}

void reconfigurarPulsos(int minUs, int maxUs) {
  // ESP32Servo aplica limites internos [500..2500] us
  int nuevoMin = constrain(minUs, 500, 1200);
  int nuevoMax = constrain(maxUs, 1500, 2500);
  if (nuevoMin >= nuevoMax) {
    Serial.println(F("[ERROR] Rango de pulso invalido."));
    return;
  }
  pulsoMinUs = nuevoMin;
  pulsoMaxUs = nuevoMax;
  miServo.detach();
  miServo.attach(pinServo, pulsoMinUs, pulsoMaxUs);
  miServo.write(anguloActual);
  Serial.printf("[SERVO] Pulsos recalibrados en GPIO %u: %d..%d us\n",
                pinServo, pulsoMinUs, pulsoMaxUs);
}

// Callback Dashboard: Slider (0..180)
void alRecibirAngulo(AdafruitIO_Data *data) {
  float valF = 0.0f;
  if (!parseFloatSeguro(data->toString(), valF)) return;
  int val = (int)roundf(valF);
  modoBarrido = false; // Al mover slider pasa automaticamente a modo manual
  fijarAnguloObjetivo(val);
}

// Callback Dashboard: "BARRIDO" / "1" o "MANUAL" / "0"
void alRecibirModo(AdafruitIO_Data *data) {
  String txt = data->toString();
  txt.trim();
  txt.toUpperCase();
  if (txt == "BARRIDO" || txt == "1" || txt == "TRUE" || txt == "ON") {
    modoBarrido = true;
    Serial.println(F("[AIO -> ESP32] Modo SERVO = BARRIDO"));
  } else {
    modoBarrido = false;
    Serial.println(F("[AIO -> ESP32] Modo SERVO = MANUAL"));
  }
}

void procesarComandoSerial(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("ANGULO=")) {
    long val = 0;
    if (parseIntSeguro(cmd.substring(7), val)) {
      modoBarrido = false;
      fijarAnguloObjetivo((int)val);
    } else {
      Serial.println(F("[ERROR] ANGULO invalido. Ej: ANGULO=90"));
    }
  } else if (cmd == "MODO=BARRIDO") {
    modoBarrido = true;
  } else if (cmd == "MODO=MANUAL") {
    modoBarrido = false;
  } else if (cmd.startsWith("VEL=")) {
    long val = 0;
    if (parseIntSeguro(cmd.substring(4), val)) {
      msPorGrado = (unsigned long)constrain(val, 2L, 200L);
      Serial.printf("[SERIAL] Velocidad = %lu ms/grado\n", msPorGrado);
    } else {
      Serial.println(F("[ERROR] VEL invalida (2..200). Ej: VEL=12"));
    }
  } else if (cmd.startsWith("LIMITES=")) {
    int coma = cmd.indexOf(',');
    if (coma > 8) {
      long mn = 0, mx = 0;
      if (parseIntSeguro(cmd.substring(8, coma), mn) &&
          parseIntSeguro(cmd.substring(coma + 1), mx) &&
          mn >= 0 && mx <= 180 && mn < mx) {
        anguloMinimo = (int)mn;
        anguloMaximo = (int)mx;
        fijarAnguloObjetivo(anguloObjetivo);
      } else {
        Serial.println(F("[ERROR] LIMITES invalidos. Ej: LIMITES=10,170"));
      }
    }
  } else if (cmd.startsWith("PULSO=")) {
    int coma = cmd.indexOf(',');
    if (coma > 6) {
      long pMin = 0, pMax = 0;
      if (parseIntSeguro(cmd.substring(6, coma), pMin) &&
          parseIntSeguro(cmd.substring(coma + 1), pMax)) {
        reconfigurarPulsos((int)pMin, (int)pMax);
      } else {
        Serial.println(F("[ERROR] PULSO invalido. Ej: PULSO=500,2400"));
      }
    }
  } else if (cmd.startsWith("PIN=")) {
    long nuevoPin = 0;
    if (parseIntSeguro(cmd.substring(4), nuevoPin) && esPinServoSeguro((int)nuevoPin)) {
      miServo.detach();
      pinServo = (uint8_t)nuevoPin;
      miServo.attach(pinServo, pulsoMinUs, pulsoMaxUs);
      miServo.write(anguloActual);
      Serial.printf("[SERIAL] Servo reasignado a GPIO %u\n", pinServo);
    } else {
      Serial.println(F("[ERROR] Pin PWM no seguro para servo (usa ej. 13, 14, 25, 26, 27, 32, 33)."));
    }
  } else {
    Serial.println(F("Comandos: ANGULO=90 | MODO=BARRIDO|MANUAL | VEL=12 | LIMITES=10,170 | PULSO=500,2400 | PIN=25"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  ESP32PWM::allocateTimer(0);
  miServo.setPeriodHertz(50);
  miServo.attach(pinServo, pulsoMinUs, pulsoMaxUs);
  miServo.write(anguloActual);

  feedServoCmd->onMessage(alRecibirAngulo);
  feedModo->onMessage(alRecibirModo);

  Serial.print(F("[AIO] Conectando Servomotor + Adafruit IO"));
  io.connect();
  unsigned long inicio = millis();
  while (io.status() < AIO_CONNECTED && millis() - inicio < 15000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println(io.statusText());

  if (io.status() >= AIO_CONNECTED) {
    feedServoCmd->get();
    feedModo->get();
    aioSincronizado = true;
  }
}

void loop() {
  io.run(10, true);

  if (!aioSincronizado && io.status() >= AIO_CONNECTED) {
    feedServoCmd->get();
    feedModo->get();
    aioSincronizado = true;
  }

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      procesarComandoSerial(bufferSerial);
      bufferSerial = "";
    } else if (bufferSerial.length() < 128) {
      bufferSerial += c;
    }
  }

  unsigned long ahora = millis();

  // Movimiento suave NO BLOQUEANTE del servomotor
  if (ahora - ultimoPasoServoMs >= msPorGrado) {
    ultimoPasoServoMs = ahora;

    if (modoBarrido) {
      anguloObjetivo = anguloActual + direccionBarrido;
      if (anguloObjetivo >= anguloMaximo) {
        anguloObjetivo = anguloMaximo;
        direccionBarrido = -1;
      } else if (anguloObjetivo <= anguloMinimo) {
        anguloObjetivo = anguloMinimo;
        direccionBarrido = 1;
      }
    }

    if (anguloActual < anguloObjetivo) {
      anguloActual++;
      miServo.write(anguloActual);
    } else if (anguloActual > anguloObjetivo) {
      anguloActual--;
      miServo.write(anguloActual);
    }
  }

  // Envio a Adafruit IO al llegar al objetivo (respetando >= 2200 ms)
  if (io.status() >= AIO_CONNECTED &&
      ahora - ultimoEnvioMs >= 2200 &&
      anguloActual == anguloObjetivo &&
      anguloActual != ultimoAnguloReportado &&
      !modoBarrido) {
    ultimoEnvioMs = ahora;
    ultimoAnguloReportado = anguloActual;
    feedServoPos->save(anguloActual);
    Serial.printf("[ESP32 -> AIO] Servo en %d grados\n", anguloActual);
  }
}
