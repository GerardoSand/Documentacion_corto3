/*
 * ============================================================================
 * SKETCH 01: LM35 CON VALORES DINAMICOS + WIFI + ADAFRUIT IO
 * ============================================================================
 * Hardware:
 *  - LM35 VCC -> VIN (5V) | GND -> GND | OUT -> GPIO 34 (ADC1_CH6)
 *  - LED Alerta (opcional) -> GPIO 26 (con resistencia 220 ohm a GND)
 *
 * Feeds en Adafruit IO:
 *  - "corto3-temp"   (ESP32 -> Dashboard): Envia temperatura en la unidad elegida
 *  - "corto3-umbral" (Dashboard Slider <-> ESP32): Cambia umbral dinamico en vivo
 *  - "corto3-modo"   (Dashboard Text/Toggle <-> ESP32): Cambia unidad ("C","F","K")
 *                     o estado de alerta / modo dinamico
 *
 * Valores Dinamicos en vivo (por Dashboard y por Monitor Serial 115200):
 *  - UMBRAL=32.5     -> Cambia el umbral de alerta (tambien desde Slider web)
 *  - HISTERESIS=1.0  -> Cambia banda muerta para evitar parpadeo en el limite
 *  - MUESTRAS=32     -> Cantidad de lecturas para filtro promedio (4..128)
 *  - OFFSET=-0.5     -> Calibracion fina de temperatura en C
 *  - UNIDAD=C|F|K    -> Cambia unidad dinamica enviada al Dashboard
 *  - PIN=34|35|32|33|36|39 -> Cambia pin ADC1 en vivo (valida que no sea ADC2!)
 *  - INTERVALO=3000  -> Intervalo minimo de envio a Adafruit IO (>= 2200 ms)
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

// ---------------------- PINES POR DEFECTO ----------------------
uint8_t pinLM35 = 34;              // Dinamico (solo ADC1 permitidos con WiFi)
constexpr uint8_t PIN_LED_ALERTA = 26;

// ---------------------- PARAMETROS DINAMICOS -------------------
float umbralAlertaC      = 30.0f;  // Modificable desde Dashboard Slider o Serial
float histeresisC        = 1.0f;   // Evita oscilaciones justo sobre el umbral
float offsetCalibracionC = 0.0f;   // Ajuste manual de calibracion (+/- C)
float factorMvPorGrado   = 10.0f;  // Datasheet LM35 = 10.0 mV por cada grado C
int   muestrasFiltro     = 32;     // Tamano dinamico de ventana de promedio
char  unidadSalida       = 'C';    // 'C', 'F' o 'K'
unsigned long intervaloEnvioMs = 3000; // >= 2200 ms para respetar 30 req/min

// ---------------------- ESTADO INTERNO -------------------------
bool alertaActiva          = false;
bool aioSincronizado       = false;
bool syncUmbralPendiente   = false;
float ultimaTempEnviada    = -999.0f;
float ultimaTempC          = 0.0f;
unsigned long ultimoMuestreoMs = 0;
unsigned long ultimoEnvioMs    = 0;
unsigned long ultimoLatidoMs   = 0;
String bufferSerial = "";

// ---------------------- ADAFRUIT IO ----------------------------
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedTemp   = io.feed("corto3-temp");
AdafruitIO_Feed *feedUmbral = io.feed("corto3-umbral");
AdafruitIO_Feed *feedModo   = io.feed("corto3-modo");

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

// Valida si un GPIO pertenece a ADC1 (seguro con WiFi encendido)
bool esPinADC1Valido(int gpio) {
  return (gpio == 32 || gpio == 33 || gpio == 34 ||
          gpio == 35 || gpio == 36 || gpio == 39);
}

// Convierte C -> C, F o K segun la unidad dinamica activa
float convertirUnidad(float tempC) {
  if (unidadSalida == 'F') return (tempC * 9.0f / 5.0f) + 32.0f;
  if (unidadSalida == 'K') return tempC + 273.15f;
  return tempC;
}

// Lee LM35 usando mV calibrados + filtro promedio recortado (elimina picos)
float leerLM35FiltradoC() {
  long sumaMv = 0;
  uint32_t minMv = 5000;
  uint32_t maxMv = 0;

  for (int i = 0; i < muestrasFiltro; i++) {
    uint32_t mv = analogReadMilliVolts(pinLM35);
    sumaMv += mv;
    if (mv < minMv) minMv = mv;
    if (mv > maxMv) maxMv = mv;
    delayMicroseconds(200); // Micro-pausa entre conversiones ADC (no bloquea WiFi)
  }

  float mvPromedio;
  if (muestrasFiltro >= 6) {
    // Descarta lectura maxima y minima para eliminar ruido impulsivo
    mvPromedio = (float)(sumaMv - minMv - maxMv) / (float)(muestrasFiltro - 2);
  } else {
    mvPromedio = (float)sumaMv / (float)muestrasFiltro;
  }

  return (mvPromedio / factorMvPorGrado) + offsetCalibracionC;
}

// Callback: recibe nuevo umbral desde Slider del Dashboard Adafruit IO
void alRecibirUmbral(AdafruitIO_Data *data) {
  float nuevo = 0.0f;
  if (parseFloatSeguro(data->toString(), nuevo) && nuevo >= -10.0f && nuevo <= 150.0f) {
    umbralAlertaC = nuevo;
    Serial.printf("[AIO -> ESP32] Nuevo umbral dinamico: %.2f C\n", umbralAlertaC);
  }
}

// Callback: recibe comando de unidad o ajuste desde Dashboard ("C", "F", "K" o "UNIDAD=C")
void alRecibirModo(AdafruitIO_Data *data) {
  String texto = data->toString();
  texto.trim();
  texto.toUpperCase();
  if (texto.startsWith("UNIDAD=")) {
    texto = texto.substring(7);
    texto.trim();
  }
  if (texto == "C" || texto == "F" || texto == "K") {
    unidadSalida = texto.charAt(0);
    ultimaTempEnviada = -999.0f; // Fuerza reenvio inmediato en nueva unidad
    Serial.printf("[AIO -> ESP32] Unidad cambiada a: %c\n", unidadSalida);
  }
}

void procesarComandoSerial(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("UMBRAL=")) {
    float val = 0.0f;
    if (parseFloatSeguro(cmd.substring(7), val) && val >= -10.0f && val <= 150.0f) {
      umbralAlertaC = val;
      syncUmbralPendiente = true;
      Serial.printf("[SERIAL] Umbral = %.2f C\n", umbralAlertaC);
    } else {
      Serial.println(F("[ERROR] UMBRAL invalido (-10..150). Ej: UMBRAL=32.5"));
    }
  } else if (cmd.startsWith("HISTERESIS=")) {
    float val = 0.0f;
    if (parseFloatSeguro(cmd.substring(11), val) && val >= 0.1f && val <= 20.0f) {
      histeresisC = val;
      Serial.printf("[SERIAL] Histeresis = %.2f C\n", histeresisC);
    } else {
      Serial.println(F("[ERROR] HISTERESIS invalida (0.1..20.0)."));
    }
  } else if (cmd.startsWith("MUESTRAS=")) {
    long val = 0;
    if (parseIntSeguro(cmd.substring(9), val)) {
      muestrasFiltro = (int)constrain(val, 4L, 128L);
      Serial.printf("[SERIAL] Muestras filtro = %d\n", muestrasFiltro);
    } else {
      Serial.println(F("[ERROR] MUESTRAS invalido (4..128). Ej: MUESTRAS=32"));
    }
  } else if (cmd.startsWith("OFFSET=")) {
    float val = 0.0f;
    if (parseFloatSeguro(cmd.substring(7), val) && val >= -50.0f && val <= 50.0f) {
      offsetCalibracionC = val;
      Serial.printf("[SERIAL] Offset calibracion = %.2f C\n", offsetCalibracionC);
    } else {
      Serial.println(F("[ERROR] OFFSET invalido (-50..50). Ej: OFFSET=-0.5"));
    }
  } else if (cmd.startsWith("UNIDAD=")) {
    String uStr = cmd.substring(7);
    uStr.trim();
    if (uStr == "C" || uStr == "F" || uStr == "K") {
      unidadSalida = uStr.charAt(0);
      ultimaTempEnviada = -999.0f;
      Serial.printf("[SERIAL] Unidad = %c\n", unidadSalida);
    } else {
      Serial.println(F("[ERROR] UNIDAD invalida. Usa UNIDAD=C, UNIDAD=F o UNIDAD=K"));
    }
  } else if (cmd.startsWith("PIN=")) {
    long nuevoPin = 0;
    if (parseIntSeguro(cmd.substring(4), nuevoPin) && esPinADC1Valido((int)nuevoPin)) {
      pinLM35 = (uint8_t)nuevoPin;
      analogSetPinAttenuation(pinLM35, ADC_11db);
      Serial.printf("[SERIAL] Pin LM35 cambiado a GPIO %u\n", pinLM35);
    } else {
      Serial.println(F("[ERROR] Solo pines ADC1 (32,33,34,35,36,39) son seguros con WiFi."));
    }
  } else if (cmd.startsWith("INTERVALO=")) {
    long val = 0;
    if (parseIntSeguro(cmd.substring(10), val)) {
      intervaloEnvioMs = (unsigned long)max(2200L, val);
      Serial.printf("[SERIAL] Intervalo envio AIO = %lu ms\n", intervaloEnvioMs);
    } else {
      Serial.println(F("[ERROR] INTERVALO invalido. Ej: INTERVALO=3000"));
    }
  } else {
    Serial.println(F("Comandos: UMBRAL=32.5 | HISTERESIS=1.0 | MUESTRAS=32 | OFFSET=0.0 | UNIDAD=C/F/K | PIN=34 | INTERVALO=3000"));
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

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_ALERTA, OUTPUT);
  digitalWrite(PIN_LED_ALERTA, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(pinLM35, ADC_11db);

  // Registrar callbacks ANTES de conectar
  feedUmbral->onMessage(alRecibirUmbral);
  feedModo->onMessage(alRecibirModo);

  Serial.print(F("[AIO] Conectando a Adafruit IO"));
  io.connect();
  unsigned long inicio = millis();
  while (io.status() < AIO_CONNECTED && millis() - inicio < 15000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println(io.statusText());

  if (io.status() >= AIO_CONNECTED) {
    // Sincroniza valores guardados previamente en el Dashboard
    feedUmbral->get();
    feedModo->get();
    aioSincronizado = true;
  }
}

void loop() {
  io.run(10, true);
  leerSerialNoBloqueante();

  if (!aioSincronizado && io.status() >= AIO_CONNECTED) {
    feedUmbral->get();
    feedModo->get();
    aioSincronizado = true;
  }

  unsigned long ahora = millis();

  // Muestreo local cada 500 ms (rapido, sin saturar internet)
  if (ahora - ultimoMuestreoMs >= 500) {
    ultimoMuestreoMs = ahora;
    ultimaTempC = leerLM35FiltradoC();

    // Logica de histeresis dinamica
    if (!alertaActiva && ultimaTempC >= umbralAlertaC) {
      alertaActiva = true;
      digitalWrite(PIN_LED_ALERTA, HIGH);
      Serial.printf("[ALERTA ON] %.2f C >= %.2f C\n", ultimaTempC, umbralAlertaC);
    } else if (alertaActiva && ultimaTempC <= (umbralAlertaC - histeresisC)) {
      alertaActiva = false;
      digitalWrite(PIN_LED_ALERTA, LOW);
      Serial.printf("[ALERTA OFF] %.2f C <= %.2f C\n", ultimaTempC, umbralAlertaC - histeresisC);
    }
  }

  // Publicacion inteligente a Adafruit IO (anti-throttle + deadband 0.25)
  if (io.status() >= AIO_CONNECTED && (ahora - ultimoEnvioMs >= intervaloEnvioMs)) {
    if (syncUmbralPendiente) {
      syncUmbralPendiente = false;
      ultimoEnvioMs = ahora;
      feedUmbral->save(umbralAlertaC);
      return;
    }

    float tempMostrar = convertirUnidad(ultimaTempC);
    bool cambioSignificativo = fabs(tempMostrar - ultimaTempEnviada) >= 0.25f;
    bool pasoLatido = (ahora - ultimoLatidoMs >= 15000);

    if (cambioSignificativo || pasoLatido) {
      ultimoEnvioMs = ahora;
      ultimoLatidoMs = ahora;
      ultimaTempEnviada = tempMostrar;

      feedTemp->save(tempMostrar, 0, 0, 0, 2); // 2 decimales de precision
      Serial.printf("[ESP32 -> AIO] Temp=%.2f %c | Umbral=%.2f C | Alerta=%s\n",
                    tempMostrar, unidadSalida, umbralAlertaC,
                    alertaActiva ? "SI" : "NO");
    }
  }
}
