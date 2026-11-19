/*
 * ============================================================================
 * SKETCH 05: SISTEMA INTEGRADO CORTO 3 (LM35 + IR + SERVO + RFID + ADAFRUIT IO)
 * ============================================================================
 * Arquitectura tipo ICPC no bloqueante con:
 *  - Maquina de estados determinista (0 delay() en loop)
 *  - Cola/Scheduler anti-throttle (nunca supera 30 datos/min en Adafruit IO)
 *  - Valores dinamicos en vivo desde Dashboard Adafruit IO y Monitor Serial
 *  - Persistencia Flash NVS (Preferences) para UIDs RFID autorizados
 *
 * Pinout Maestro Sin Conflictos (ESP32 DevKit):
 *  - LM35 OUT        -> GPIO 34 (ADC1_CH6, VCC del LM35 a VIN 5V)
 *  - IR Obstaculo DO -> GPIO 32 (VCC a 3.3V)
 *  - IR Obstaculo AO -> GPIO 35 (opcional, ADC1_CH7)
 *  - Servomotor PWM  -> GPIO 25 (VCC a 5V EXTERNOS + GND comun)
 *  - RFID MFRC522    -> SCK=18, MISO=19, MOSI=23, SS=5, RST=4 (VCC 3.3V ESTRICTO)
 *  - LED Alerta      -> GPIO 26 (opcional)
 *
 * Feeds Adafruit IO (8 feeds <= limite gratuito de 10):
 *  1. "corto3-temp"      (ESP32 -> Web): Temperatura en C
 *  2. "corto3-ir"        (ESP32 -> Web): Estado IR y conteo ("LIBRE|CNT:0")
 *  3. "corto3-servo"     (ESP32 -> Web): Angulo actual del servo
 *  4. "corto3-rfid"      (ESP32 -> Web): Ultimo evento RFID ("OK|AA:BB:CC:DD")
 *  5. "corto3-umbral"    (Web Slider -> ESP32): Umbral dinamico LM35 (C)
 *  6. "corto3-servo-cmd" (Web Slider -> ESP32): Angulo manual del servo (0..180)
 *  7. "corto3-modo"      (Web Toggle/Text -> ESP32): "AUTO" o "MANUAL"
 *  8. "corto3-rfid-cmd"  (Web Text/Btn -> ESP32): LEARN, ADD:UID, DEL:UID, RESET_IR
 * ============================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <Preferences.h>
#include <vector>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     "TU_WIFI_SSID"
  #define WIFI_PASS     "TU_WIFI_PASSWORD"
  #define IO_USERNAME   "TU_USUARIO_ADAFRUIT"
  #define IO_KEY        "aio_TU_LLAVE_SECRETA_AQUI"
#endif

#include "AdafruitIO_WiFi.h"

// ---------------------- PINES MAESTROS ------------------------
uint8_t pinLM35                 = 34;
constexpr uint8_t PIN_IR_DO     = 32;
uint8_t pinServo                = 25;
constexpr uint8_t PIN_LED       = 26;
constexpr uint8_t PIN_RFID_SS   = 5;
constexpr uint8_t PIN_RFID_RST  = 4;
constexpr uint8_t PIN_SPI_SCK   = 18;
constexpr uint8_t PIN_SPI_MISO  = 19;
constexpr uint8_t PIN_SPI_MOSI  = 23;

// ---------------------- OBJETOS -------------------------------
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo servoCompuerta;
Preferences prefs;

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedTemp     = io.feed("corto3-temp");
AdafruitIO_Feed *feedIR       = io.feed("corto3-ir");
AdafruitIO_Feed *feedServo    = io.feed("corto3-servo");
AdafruitIO_Feed *feedRFID     = io.feed("corto3-rfid");
AdafruitIO_Feed *feedUmbral   = io.feed("corto3-umbral");
AdafruitIO_Feed *feedServoCmd = io.feed("corto3-servo-cmd");
AdafruitIO_Feed *feedModo     = io.feed("corto3-modo");
AdafruitIO_Feed *feedCmd      = io.feed("corto3-rfid-cmd");

// ---------------------- PARAMETROS DINAMICOS ------------------
float umbralTempC         = 32.0f;
float histeresisTempC     = 1.0f;
bool  tempAbreServo       = true;   // Si hay sobretemperatura, abre compuerta
bool  seguridadIR         = true;   // Si IR detecta obstaculo, impide cerrar compuerta
bool  irActivoEnBajo      = true;   // FC-51 entrega LOW al detectar
int   anguloCerrado       = 0;
int   anguloAbierto       = 90;
unsigned long tiempoAperturaMs = 4000;
unsigned long msPorGrado       = 10;

bool modoManual           = false;  // false = AUTO | true = MANUAL (Slider)
int  anguloManualObjetivo = 0;

// ---------------------- ESTADO DEL SISTEMA --------------------
float tempActualC         = 0.0f;
bool  alertaTermica       = false;
bool  irDetectado         = false;
bool  irPrevio            = false;
unsigned long conteoIR    = 0;

bool  compuertaAbiertaPorRFID = false;
unsigned long inicioAperturaMs = 0;
int   anguloActual        = 0;
int   anguloObjetivo      = 0;

std::vector<String> listaUIDs;
bool modoLearnRFID        = false;
bool aioSincronizado      = false;
String ultimoEventoRFID   = "INIT";

// ---------------------- TIMERS Y ANTI-THROTTLE ----------------
unsigned long ultimoLM35Ms      = 0;
unsigned long ultimoIRCambioMs  = 0;
unsigned long ultimoPasoServoMs = 0;
unsigned long ultimaLecturaRFID = 0;
unsigned long ultimoEnvioAioMs  = 0;
unsigned long ultimoLatidoMs    = 0;

const unsigned long INTERVALO_GLOBAL_AIO_MS = 2300; // <= 26 envios/min (margen seguro)
bool dirtyRFID  = false;
bool dirtyServo = false;
bool dirtyTemp  = false;
bool dirtyIR    = false;
float ultimaTempEnviada = -999.0f;
int ultimoServoEnviado  = -1;
uint8_t turnoRoundRobin = 0;

String bufferSerial = "";

// ---------------------- UTILIDADES NVS RFID -------------------
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

bool esPinADC1Valido(int gpio) {
  return (gpio == 32 || gpio == 33 || gpio == 34 ||
          gpio == 35 || gpio == 36 || gpio == 39);
}

bool esPinServoSeguro(int gpio) {
  return (gpio == 13 || gpio == 14 || gpio == 16 || gpio == 17 ||
          gpio == 25 || gpio == 27 || gpio == 33);
}

String normUID(String s) {
  s.trim();
  s.toUpperCase();
  String hexLimpio = "";
  for (size_t i = 0; i < (size_t)s.length(); i++) {
    char c = s.charAt(i);
    if (c == ':' || c == '-' || c == ' ') continue;
    if (!isxdigit((unsigned char)c)) return "";
    hexLimpio += c;
  }
  if (hexLimpio.length() < 8 || hexLimpio.length() > 20 || (hexLimpio.length() % 2) != 0) {
    return "";
  }
  String out = "";
  for (size_t i = 0; i < (size_t)hexLimpio.length(); i += 2) {
    if (i > 0) out += ":";
    out += hexLimpio.substring(i, i + 2);
  }
  return out;
}

String uidToStr(const MFRC522::Uid &u) {
  String s = "";
  for (byte i = 0; i < u.size; i++) {
    if (u.uidByte[i] < 0x10) s += "0";
    s += String(u.uidByte[i], HEX);
    if (i + 1 < u.size) s += ":";
  }
  s.toUpperCase();
  return s;
}

int buscarUID(const String &uid) {
  for (size_t i = 0; i < listaUIDs.size(); i++) {
    if (listaUIDs[i] == uid) return (int)i;
  }
  return -1;
}

void guardarUIDs() {
  String raw = "";
  for (size_t i = 0; i < listaUIDs.size(); i++) {
    if (i > 0) raw += ",";
    raw += listaUIDs[i];
  }
  prefs.begin("corto3_rfid", false);
  prefs.putString("uids", raw);
  prefs.end();
}

void cargarUIDs() {
  listaUIDs.clear();
  prefs.begin("corto3_rfid", true);
  String raw = prefs.getString("uids", "");
  prefs.end();
  int pos = 0;
  while (pos < (int)raw.length()) {
    int c = raw.indexOf(',', pos);
    if (c == -1) c = raw.length();
    String u = normUID(raw.substring(pos, c));
    if (u.length() >= 4) listaUIDs.push_back(u);
    pos = c + 1;
  }
}

void agregarUID(String uid) {
  uid = normUID(uid);
  if (uid.length() < 11) {
    Serial.println(F("[ERROR] UID invalido. Usa formato AA:BB:CC:DD o AABBCCDD."));
    return;
  }
  if (buscarUID(uid) == -1) {
    listaUIDs.push_back(uid);
    guardarUIDs();
    Serial.printf("[RFID] Autorizado agregado: %s\n", uid.c_str());
  }
}

void eliminarUID(String uid) {
  uid = normUID(uid);
  if (uid.length() < 11) {
    Serial.println(F("[ERROR] UID invalido para eliminar."));
    return;
  }
  int idx = buscarUID(uid);
  if (idx >= 0) {
    listaUIDs.erase(listaUIDs.begin() + idx);
    guardarUIDs();
    Serial.printf("[RFID] Eliminado: %s\n", uid.c_str());
  }
}

// ---------------------- PROCESAMIENTO DE COMANDOS -------------
void aplicarComando(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd == "LEARN") {
    modoLearnRFID = true;
    Serial.println(F("[CMD] Modo LEARN activo: acerca la tarjeta."));
  } else if (cmd.startsWith("ADD:")) {
    agregarUID(cmd.substring(4));
  } else if (cmd.startsWith("DEL:")) {
    eliminarUID(cmd.substring(4));
  } else if (cmd == "CLEAR_RFID" || cmd == "CLEAR") {
    listaUIDs.clear();
    guardarUIDs();
    Serial.println(F("[CMD] Lista RFID vaciada."));
  } else if (cmd == "LIST_RFID" || cmd == "LIST") {
    Serial.printf("[RFID] Total=%u\n", (unsigned)listaUIDs.size());
    for (size_t i = 0; i < listaUIDs.size(); i++) {
      Serial.printf("  - %s\n", listaUIDs[i].c_str());
    }
  } else if (cmd == "RESET_IR") {
    conteoIR = 0;
    dirtyIR = true;
    Serial.println(F("[CMD] Conteo IR reiniciado."));
  } else if (cmd == "ABRIR") {
    modoManual = false;
    compuertaAbiertaPorRFID = true;
    inicioAperturaMs = millis();
  } else if (cmd.startsWith("UMBRAL=")) {
    float v = 0.0f;
    if (parseFloatSeguro(cmd.substring(7), v) && v >= -10.0f && v <= 150.0f) {
      umbralTempC = v;
      Serial.printf("[CMD] Umbral LM35 = %.2f C\n", umbralTempC);
    } else {
      Serial.println(F("[ERROR] UMBRAL invalido (-10..150)."));
    }
  } else if (cmd.startsWith("ANG_OPEN=")) {
    long v = 0;
    if (parseIntSeguro(cmd.substring(9), v)) {
      anguloAbierto = (int)constrain(v, 0L, 180L);
    } else {
      Serial.println(F("[ERROR] ANG_OPEN invalido (0..180)."));
    }
  } else if (cmd.startsWith("ANG_CLOSE=")) {
    long v = 0;
    if (parseIntSeguro(cmd.substring(10), v)) {
      anguloCerrado = (int)constrain(v, 0L, 180L);
    } else {
      Serial.println(F("[ERROR] ANG_CLOSE invalido (0..180)."));
    }
  } else if (cmd.startsWith("TIEMPO_OPEN=")) {
    long v = 0;
    if (parseIntSeguro(cmd.substring(12), v)) {
      tiempoAperturaMs = (unsigned long)constrain(v, 500L, 60000L);
    } else {
      Serial.println(F("[ERROR] TIEMPO_OPEN invalido (500..60000 ms)."));
    }
  } else if (cmd.startsWith("VEL=")) {
    long v = 0;
    if (parseIntSeguro(cmd.substring(4), v)) {
      msPorGrado = (unsigned long)constrain(v, 2L, 200L);
    } else {
      Serial.println(F("[ERROR] VEL invalida (2..200 ms/grado)."));
    }
  } else if (cmd.startsWith("PIN_SERVO=")) {
    long p = 0;
    if (parseIntSeguro(cmd.substring(10), p) && esPinServoSeguro((int)p)) {
      servoCompuerta.detach();
      pinServo = (uint8_t)p;
      servoCompuerta.attach(pinServo, 500, 2400);
      servoCompuerta.write(anguloActual);
      Serial.printf("[CMD] Pin servo cambiado a GPIO %u\n", pinServo);
    } else {
      Serial.println(F("[ERROR] Pin servo invalido (usa 13,14,16,17,25,27,33)."));
    }
  } else if (cmd.startsWith("PIN_LM35=")) {
    long p = 0;
    if (parseIntSeguro(cmd.substring(9), p) && esPinADC1Valido((int)p) && p != PIN_IR_DO) {
      pinLM35 = (uint8_t)p;
      analogSetPinAttenuation(pinLM35, ADC_11db);
      Serial.printf("[CMD] Pin LM35 cambiado a GPIO %u\n", pinLM35);
    } else {
      Serial.println(F("[ERROR] Pin LM35 invalido (usa ADC1 libre: 33,34,35,36,39)."));
    }
  } else if (cmd == "IR_ACTIVO=LOW") {
    irActivoEnBajo = true;
  } else if (cmd == "IR_ACTIVO=HIGH") {
    irActivoEnBajo = false;
  } else if (cmd == "SEGURIDAD_IR=ON") {
    seguridadIR = true;
  } else if (cmd == "SEGURIDAD_IR=OFF") {
    seguridadIR = false;
  } else if (cmd == "TEMP_ABRE=ON") {
    tempAbreServo = true;
  } else if (cmd == "TEMP_ABRE=OFF") {
    tempAbreServo = false;
  } else if (cmd == "MODO=MANUAL") {
    modoManual = true;
  } else if (cmd == "MODO=AUTO") {
    modoManual = false;
  } else if (cmd == "STATUS") {
    Serial.printf("[STATUS] T=%.2fC (U=%.1f) | IR=%s(%lu) | Servo=%d->%d (GPIO %u) | Modo=%s | AIO=%s\n",
                  tempActualC, umbralTempC, irDetectado ? "DET" : "LIB",
                  conteoIR, anguloActual, anguloObjetivo, pinServo,
                  modoManual ? "MANUAL" : "AUTO",
                  io.status() >= AIO_CONNECTED ? "ON" : "OFF");
  } else {
    Serial.println(F("Cmds: LEARN | ADD:UID | DEL:UID | LIST_RFID | CLEAR_RFID | RESET_IR | ABRIR | UMBRAL=32 | ANG_OPEN=90 | ANG_CLOSE=0 | TIEMPO_OPEN=4000 | VEL=10 | PIN_SERVO=25 | PIN_LM35=34 | MODO=AUTO|MANUAL | STATUS"));
  }
}

// ---------------------- CALLBACKS ADAFRUIT IO -----------------
void onUmbral(AdafruitIO_Data *d) {
  float v = 0.0f;
  if (parseFloatSeguro(d->toString(), v) && v >= 0.0f && v <= 100.0f) {
    umbralTempC = v;
    Serial.printf("[AIO] Umbral LM35 = %.2f C\n", umbralTempC);
  }
}

void onServoCmd(AdafruitIO_Data *d) {
  float vf = 0.0f;
  if (!parseFloatSeguro(d->toString(), vf)) return;
  anguloManualObjetivo = (int)constrain((long)roundf(vf), 0L, 180L);
  modoManual = true;
  Serial.printf("[AIO] Slider Servo -> MANUAL %d grados\n", anguloManualObjetivo);
}

void onModo(AdafruitIO_Data *d) {
  String m = d->toString();
  m.trim();
  m.toUpperCase();
  modoManual = (m == "MANUAL" || m == "1" || m == "TRUE" || m == "ON");
  Serial.printf("[AIO] Modo = %s\n", modoManual ? "MANUAL" : "AUTO");
}

void onCmd(AdafruitIO_Data *d) {
  aplicarComando(d->toString());
}

// ---------------------- TAREAS NO BLOQUEANTES -----------------
void actualizarLM35(unsigned long ahora) {
  if (ahora - ultimoLM35Ms < 500) return;
  ultimoLM35Ms = ahora;

  long suma = 0;
  for (int i = 0; i < 16; i++) {
    suma += analogReadMilliVolts(pinLM35);
    delayMicroseconds(150);
  }
  tempActualC = (suma / 16.0f) / 10.0f;

  if (!alertaTermica && tempActualC >= umbralTempC) {
    alertaTermica = true;
    dirtyTemp = true;
    Serial.printf("[LM35] ALERTA TERMICA! %.2f C >= %.2f C\n", tempActualC, umbralTempC);
  } else if (alertaTermica && tempActualC <= (umbralTempC - histeresisTempC)) {
    alertaTermica = false;
    dirtyTemp = true;
    Serial.printf("[LM35] Temperatura normalizada: %.2f C\n", tempActualC);
  } else if (fabs(tempActualC - ultimaTempEnviada) >= 0.5f) {
    dirtyTemp = true;
  }
}

void actualizarIR(unsigned long ahora) {
  int nivel = digitalRead(PIN_IR_DO);
  bool raw = irActivoEnBajo ? (nivel == LOW) : (nivel == HIGH);

  if (raw != irPrevio && (ahora - ultimoIRCambioMs >= 200)) {
    ultimoIRCambioMs = ahora;
    irPrevio = raw;
    irDetectado = raw;
    if (irDetectado) conteoIR++;
    dirtyIR = true;
    Serial.printf("[IR] %s | Conteo=%lu\n", irDetectado ? "OBSTACULO" : "LIBRE", conteoIR);
  }
}

void actualizarRFID(unsigned long ahora) {
  if (ahora - ultimaLecturaRFID < 2000) return;
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  ultimaLecturaRFID = ahora;
  String uid = uidToStr(rfid.uid);

  if (modoLearnRFID) {
    modoLearnRFID = false;
    agregarUID(uid);
    ultimoEventoRFID = "LEARN|" + uid;
    compuertaAbiertaPorRFID = true;
    inicioAperturaMs = ahora;
  } else if (buscarUID(uid) >= 0) {
    ultimoEventoRFID = "OK|" + uid;
    compuertaAbiertaPorRFID = true;
    inicioAperturaMs = ahora;
    Serial.printf("[RFID] ACCESO OK: %s\n", uid.c_str());
  } else {
    ultimoEventoRFID = "DENIED|" + uid;
    Serial.printf("[RFID] ACCESO DENEGADO: %s\n", uid.c_str());
  }

  dirtyRFID = true;
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void calcularObjetivoServo(unsigned long ahora) {
  if (modoManual) {
    // Incluso en manual, si la seguridad IR esta activa y se intenta cerrar, protege
    bool intentaCerrar = abs(anguloManualObjetivo - anguloCerrado) < abs(anguloActual - anguloCerrado);
    if (seguridadIR && irDetectado && intentaCerrar) {
      anguloObjetivo = anguloActual;
    } else {
      anguloObjetivo = anguloManualObjetivo;
    }
    return;
  }

  // Modo AUTO:
  if (compuertaAbiertaPorRFID) {
    if (seguridadIR && irDetectado) {
      // Si hay obstaculo en la compuerta, reinicia temporizador de cierre (anti-aplastamiento)
      inicioAperturaMs = ahora;
    }
    if (ahora - inicioAperturaMs >= tiempoAperturaMs) {
      compuertaAbiertaPorRFID = false;
    }
  }

  bool abrirPorTemp = (tempAbreServo && alertaTermica);
  if (compuertaAbiertaPorRFID || abrirPorTemp) {
    anguloObjetivo = anguloAbierto;
  } else if (seguridadIR && irDetectado && anguloActual != anguloCerrado) {
    // Mantiene posicion actual mientras haya obstaculo (funciona incluso si ANG_CLOSE > ANG_OPEN)
    anguloObjetivo = anguloActual;
  } else {
    anguloObjetivo = anguloCerrado;
  }
}

void moverServoSuave(unsigned long ahora) {
  if (ahora - ultimoPasoServoMs < msPorGrado) return;
  ultimoPasoServoMs = ahora;

  if (anguloActual < anguloObjetivo) {
    anguloActual++;
    servoCompuerta.write(anguloActual);
  } else if (anguloActual > anguloObjetivo) {
    anguloActual--;
    servoCompuerta.write(anguloActual);
  }

  if (anguloActual == anguloObjetivo && anguloActual != ultimoServoEnviado) {
    dirtyServo = true;
  }

  // LED refleja alerta termica o compuerta abierta
  digitalWrite(PIN_LED, (alertaTermica || compuertaAbiertaPorRFID) ? HIGH : LOW);
}

// Scheduler anti-throttle: envia maximo 1 feed cada 2.3 segundos por prioridad
void despacharAdafruitIO(unsigned long ahora) {
  if (io.status() < AIO_CONNECTED) return;
  if (ahora - ultimoEnvioAioMs < INTERVALO_GLOBAL_AIO_MS) return;

  // Latido periodico para mantener graficas vivas si nada cambio en 20 s
  if (!dirtyRFID && !dirtyServo && !dirtyTemp && !dirtyIR &&
      (ahora - ultimoLatidoMs >= 20000)) {
    ultimoLatidoMs = ahora;
    dirtyTemp = true;
  }

  if (dirtyRFID) {
    dirtyRFID = false;
    ultimoEnvioAioMs = ahora;
    feedRFID->save(ultimoEventoRFID);
    Serial.printf("[AIO ->] RFID=%s\n", ultimoEventoRFID.c_str());
    return;
  }

  if (dirtyServo) {
    dirtyServo = false;
    ultimoEnvioAioMs = ahora;
    ultimoServoEnviado = anguloActual;
    feedServo->save(anguloActual);
    Serial.printf("[AIO ->] Servo=%d\n", anguloActual);
    return;
  }

  // Alterna entre Temp e IR si ambos quieren enviar
  for (int intento = 0; intento < 2; intento++) {
    uint8_t canal = (turnoRoundRobin + intento) % 2;
    if (canal == 0 && dirtyTemp) {
      dirtyTemp = false;
      turnoRoundRobin = 1;
      ultimoEnvioAioMs = ahora;
      ultimoLatidoMs = ahora;
      ultimaTempEnviada = tempActualC;
      feedTemp->save(tempActualC, 0, 0, 0, 2);
      Serial.printf("[AIO ->] Temp=%.2f C\n", tempActualC);
      return;
    }
    if (canal == 1 && dirtyIR) {
      dirtyIR = false;
      turnoRoundRobin = 0;
      ultimoEnvioAioMs = ahora;
      String msg = String(irDetectado ? "DETECTA" : "LIBRE") + "|CNT:" + String(conteoIR);
      feedIR->save(msg);
      Serial.printf("[AIO ->] IR=%s\n", msg.c_str());
      return;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\n=== SISTEMA INTEGRADO CORTO 3 (ICPC EDITION) ==="));

  pinMode(PIN_IR_DO, INPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(pinLM35, ADC_11db);

  ESP32PWM::allocateTimer(0);
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(pinServo, 500, 2400);
  servoCompuerta.write(anguloCerrado);

  cargarUIDs();

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  delay(50);
  Serial.printf("[RFID] Version MFRC522: 0x%02X | UIDs guardados: %u\n",
                rfid.PCD_ReadRegister(MFRC522::VersionReg),
                (unsigned)listaUIDs.size());

  feedUmbral->onMessage(onUmbral);
  feedServoCmd->onMessage(onServoCmd);
  feedModo->onMessage(onModo);
  feedCmd->onMessage(onCmd);

  Serial.print(F("[AIO] Conectando"));
  io.connect();
  unsigned long inicio = millis();
  while (io.status() < AIO_CONNECTED && millis() - inicio < 15000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println(io.statusText());

  if (io.status() >= AIO_CONNECTED) {
    feedUmbral->get();
    feedModo->get();
    aioSincronizado = true;
  }
}

void loop() {
  io.run(10, true);

  if (!aioSincronizado && io.status() >= AIO_CONNECTED) {
    feedUmbral->get();
    feedModo->get();
    aioSincronizado = true;
  }

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      aplicarComando(bufferSerial);
      bufferSerial = "";
    } else if (bufferSerial.length() < 128) {
      bufferSerial += c;
    }
  }

  unsigned long ahora = millis();
  actualizarLM35(ahora);
  actualizarIR(ahora);
  actualizarRFID(ahora);
  calcularObjetivoServo(ahora);
  moverServoSuave(ahora);
  despacharAdafruitIO(ahora);
}
