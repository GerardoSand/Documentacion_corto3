/*
 * ============================================================================
 * SKETCH 02: SENSOR INFRARROJO VERSATIL + WIFI + ADAFRUIT IO
 * ============================================================================
 * Soporta los 3 modos de sensor IR mas comunes en laboratorio:
 *  1) DO (Obstaculo / Seguidor de linea FC-51 / KY-032 / TCRT5000) -> GPIO 32
 *  2) AO (Lectura analogica proporcional con umbral dinamico)     -> GPIO 35
 *  3) PIR (Sensor de movimiento HC-SR501 conectado en GPIO 32)
 *
 * Feeds en Adafruit IO:
 *  - "corto3-ir"       (ESP32 -> Dashboard): Estado + Conteo ("LIBRE|CNT:5")
 *  - "corto3-umbral"   (Dashboard Slider <-> ESP32): Umbral analogico (mV o %)
 *  - "corto3-modo"     (Dashboard Text <-> ESP32): "DO", "AO" o "PIR"
 *  - "corto3-rfid-cmd" (Dashboard Button/Text -> ESP32): "RESET_IR", "CALIBRAR"
 *
 * Comandos dinamicos por Serial (115200):
 *  - MODO=DO | MODO=AO | MODO=PIR
 *  - ACTIVO=LOW | ACTIVO=HIGH   (invierte polaridad logica en tiempo real)
 *  - UMBRAL_MV=1500             (umbral dinamico para modo AO en milivoltios)
 *  - DEBOUNCE=250               (antirrebote dinamico en ms)
 *  - CALIBRAR                   (toma lectura actual como linea base y fija umbral)
 *  - RESET                      (reinicia el contador a 0)
 * ============================================================================
 */

#include <Arduino.h>
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

// ---------------------- PINES SEGUROS ----------------------
constexpr uint8_t PIN_IR_DO  = 32; // Entrada digital segura (ADC1 tambien)
constexpr uint8_t PIN_IR_AO  = 35; // Entrada analogica pura ADC1_CH7
constexpr uint8_t PIN_LED_IR = 26; // LED indicador opcional

// ---------------------- PARAMETROS DINAMICOS ---------------
enum ModoIR { MODO_DO, MODO_AO, MODO_PIR };
ModoIR modoActual = MODO_DO;

bool activoEnBajo        = true;   // FC-51 suele dar LOW al detectar obstaculo
bool analogMenorDetecta  = true;   // En TCRT5000/FC-51 menor mV = mas cerca/blanco
uint32_t umbralAnalogMv  = 1600;   // Umbral dinamico en mV (0..3300)
unsigned long debounceMs = 200;    // Antirrebote dinamico entre conteos

// ---------------------- ESTADO INTERNO ---------------------
bool estadoDetectado       = false;
bool estadoAnterior        = false;
bool aioSincronizado       = false;
unsigned long conteoEventos = 0;
unsigned long ultimoCambioMs = 0;
unsigned long ultimoEnvioMs  = 0;
unsigned long ultimoLatidoMs = 0;
bool hayPendienteEnvio       = true;
String bufferSerial          = "";

// ---------------------- ADAFRUIT IO ------------------------
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedIR     = io.feed("corto3-ir");
AdafruitIO_Feed *feedUmbral = io.feed("corto3-umbral");
AdafruitIO_Feed *feedModo   = io.feed("corto3-modo");
AdafruitIO_Feed *feedCmd    = io.feed("corto3-rfid-cmd");

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

const char* nombreModo() {
  if (modoActual == MODO_AO) return "AO";
  if (modoActual == MODO_PIR) return "PIR";
  return "DO";
}

// Calibra el umbral analogico dinamicamente tomando 20 muestras en reposo
void calibrarUmbralDinamico() {
  long suma = 0;
  for (int i = 0; i < 20; i++) {
    suma += analogReadMilliVolts(PIN_IR_AO);
    delayMicroseconds(200);
  }
  uint32_t baseMv = (uint32_t)(suma / 20);
  if (analogMenorDetecta) {
    umbralAnalogMv = (baseMv > 400) ? (baseMv - 400) : 200;
  } else {
    umbralAnalogMv = min((uint32_t)3100, baseMv + 400);
  }
  hayPendienteEnvio = true;
  Serial.printf("[IR CALIBRADO] Base=%u mV -> Nuevo umbral=%u mV\n", baseMv, umbralAnalogMv);
}

// Lee el sensor segun el modo dinamico seleccionado
bool leerSensorIR(uint32_t &outMv) {
  outMv = analogReadMilliVolts(PIN_IR_AO);

  if (modoActual == MODO_AO) {
    return analogMenorDetecta ? (outMv <= umbralAnalogMv)
                              : (outMv >= umbralAnalogMv);
  }

  // MODO_DO o MODO_PIR usan lectura digital
  int nivel = digitalRead(PIN_IR_DO);
  if (modoActual == MODO_PIR) {
    return (nivel == HIGH); // PIR HC-SR501 entrega HIGH cuando hay movimiento
  }
  return activoEnBajo ? (nivel == LOW) : (nivel == HIGH);
}

// Callback Dashboard: Slider cambia umbral analogico (acepta mV 0..3300 o % 0..100)
void alRecibirUmbral(AdafruitIO_Data *data) {
  float valF = 0.0f;
  if (!parseFloatSeguro(data->toString(), valF)) return;
  long val = (long)roundf(valF);
  if (val >= 0 && val <= 100) {
    // Si el slider esta en 0..100 (porcentaje o compartido con LM35), escala a mV
    umbralAnalogMv = (uint32_t)map(val, 0, 100, 100, 3200);
  } else {
    umbralAnalogMv = (uint32_t)constrain(val, 50L, 3300L);
  }
  Serial.printf("[AIO -> ESP32] Umbral IR AO = %u mV\n", umbralAnalogMv);
}

void alRecibirModo(AdafruitIO_Data *data) {
  String txt = data->toString();
  txt.trim();
  txt.toUpperCase();
  if (txt.startsWith("MODO=")) {
    txt = txt.substring(5);
    txt.trim();
  }
  if (txt == "DO") modoActual = MODO_DO;
  else if (txt == "AO") modoActual = MODO_AO;
  else if (txt == "PIR") modoActual = MODO_PIR;
  else return;

  Serial.printf("[AIO -> ESP32] Modo IR = %s\n", nombreModo());
  hayPendienteEnvio = true;
}

void ejecutarComandoIR(String cmd, const char *origen) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd == "MODO=DO" || cmd == "DO") modoActual = MODO_DO;
  else if (cmd == "MODO=AO" || cmd == "AO") modoActual = MODO_AO;
  else if (cmd == "MODO=PIR" || cmd == "PIR") modoActual = MODO_PIR;
  else if (cmd == "ACTIVO=LOW") activoEnBajo = true;
  else if (cmd == "ACTIVO=HIGH") activoEnBajo = false;
  else if (cmd == "ANALOG_POL=MENOR") analogMenorDetecta = true;
  else if (cmd == "ANALOG_POL=MAYOR") analogMenorDetecta = false;
  else if (cmd.startsWith("UMBRAL_MV=")) {
    long val = 0;
    if (parseIntSeguro(cmd.substring(10), val)) {
      umbralAnalogMv = (uint32_t)constrain(val, 50L, 3300L);
    } else {
      Serial.println(F("[ERROR] UMBRAL_MV invalido (50..3300)."));
      return;
    }
  } else if (cmd.startsWith("DEBOUNCE=")) {
    long val = 0;
    if (parseIntSeguro(cmd.substring(9), val)) {
      debounceMs = (unsigned long)constrain(val, 20L, 5000L);
    } else {
      Serial.println(F("[ERROR] DEBOUNCE invalido (20..5000 ms)."));
      return;
    }
  } else if (cmd == "CALIBRAR") {
    calibrarUmbralDinamico();
  } else if (cmd == "RESET" || cmd == "RESET_IR") {
    conteoEventos = 0;
    hayPendienteEnvio = true;
  } else {
    Serial.println(F("Comandos: MODO=DO|AO|PIR | ACTIVO=LOW|HIGH | ANALOG_POL=MENOR|MAYOR | UMBRAL_MV=1600 | DEBOUNCE=200 | CALIBRAR | RESET_IR"));
    return;
  }

  hayPendienteEnvio = true;
  Serial.printf("[%s] Modo=%s | Activo=%s | Umbral=%u mV | Debounce=%lu ms | Cnt=%lu\n",
                origen, nombreModo(), activoEnBajo ? "LOW" : "HIGH",
                umbralAnalogMv, debounceMs, conteoEventos);
}

void alRecibirComando(AdafruitIO_Data *data) {
  ejecutarComandoIR(data->toString(), "AIO -> ESP32");
}

void procesarComandoSerial(String cmd) {
  ejecutarComandoIR(cmd, "SERIAL");
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

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_IR_DO, INPUT);
  pinMode(PIN_LED_IR, OUTPUT);
  digitalWrite(PIN_LED_IR, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_IR_AO, ADC_11db);

  feedUmbral->onMessage(alRecibirUmbral);
  feedModo->onMessage(alRecibirModo);
  feedCmd->onMessage(alRecibirComando);

  Serial.print(F("[AIO] Conectando IR + Adafruit IO"));
  io.connect();
  unsigned long inicio = millis();
  while (io.status() < AIO_CONNECTED && millis() - inicio < 15000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println(io.statusText());

  if (io.status() >= AIO_CONNECTED) {
    feedModo->get();
    feedUmbral->get();
    aioSincronizado = true;
  }
}

void loop() {
  io.run(10, true);
  leerSerialNoBloqueante();

  if (!aioSincronizado && io.status() >= AIO_CONNECTED) {
    feedModo->get();
    feedUmbral->get();
    aioSincronizado = true;
  }

  unsigned long ahora = millis();
  uint32_t lecturaMv = 0;
  bool crudo = leerSensorIR(lecturaMv);

  // Antirrebote temporal + deteccion de flanco de entrada
  if (crudo != estadoAnterior && (ahora - ultimoCambioMs >= debounceMs)) {
    ultimoCambioMs = ahora;
    estadoAnterior = crudo;
    estadoDetectado = crudo;
    digitalWrite(PIN_LED_IR, estadoDetectado ? HIGH : LOW);

    if (estadoDetectado) {
      conteoEventos++;
    }
    hayPendienteEnvio = true;

    Serial.printf("[IR %s] %s | Conteo=%lu | AO=%u mV\n",
                  nombreModo(),
                  estadoDetectado ? "DETECTADO" : "LIBRE",
                  conteoEventos,
                  lecturaMv);
  }

  // Envio anti-throttle a Adafruit IO (minimo 2200 ms entre publicaciones)
  bool pasoLatido = (ahora - ultimoLatidoMs >= 15000);
  if (io.status() >= AIO_CONNECTED &&
      (ahora - ultimoEnvioMs >= 2200) &&
      (hayPendienteEnvio || pasoLatido)) {
    ultimoEnvioMs = ahora;
    ultimoLatidoMs = ahora;
    hayPendienteEnvio = false;

    String payload = String(estadoDetectado ? "DETECTA" : "LIBRE") +
                     "|CNT:" + String(conteoEventos) +
                     "|" + String(nombreModo());
    feedIR->save(payload);
    Serial.print(F("[ESP32 -> AIO] "));
    Serial.println(payload);
  }
}
