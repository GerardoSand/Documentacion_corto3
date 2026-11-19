/*
 * ============================================================================
 * SKETCH 04: RFID MFRC522 DINAMICO + PERSISTENCIA NVS + WIFI + ADAFRUIT IO
 * ============================================================================
 * Hardware (ESP32 VSPI - ALIMENTACION ESTRICTA A 3.3V!):
 *  - VCC -> 3.3V (NUNCA 5V!) | GND -> GND
 *  - SDA(SS) -> GPIO 5  | SCK -> GPIO 18 | MOSI -> GPIO 23 | MISO -> GPIO 19
 *  - RST     -> GPIO 4  (deja libres 21/22 para OLED I2C)
 *  - LED Acceso OK (opcional) -> GPIO 26
 *
 * Feeds en Adafruit IO:
 *  - "corto3-rfid"     (ESP32 -> Dashboard): Resultado ("OK|AA:BB:CC:DD")
 *  - "corto3-rfid-cmd" (Dashboard Text/Button -> ESP32): Comandos dinamicos
 *
 * Capacidades Dinamicas (Dashboard "corto3-rfid-cmd" y Serial 115200):
 *  - LEARN           -> La siguiente tarjeta que acerques se autoriza y guarda en Flash
 *  - ADD:AA:BB:CC:DD -> Agrega un UID manualmente sin acercar la tarjeta
 *  - DEL:AA:BB:CC:DD -> Elimina un UID de la lista autorizada
 *  - LIST            -> Imprime en Serial todos los UIDs guardados en NVS
 *  - CLEAR           -> Borra todas las tarjetas autorizadas
 *  - READ_BLOQUE     -> Activa lectura de bloque 4 MIFARE Classic en siguiente tarjeta
 *  - WRITE_BLOQUE=TXT-> Escribe hasta 16 caracteres dinamicos en bloque 4 de tarjeta
 * ============================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
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

// ---------------------- PINES VSPI + LED -----------------------
constexpr uint8_t PIN_RFID_SS   = 5;
constexpr uint8_t PIN_RFID_RST  = 4;
constexpr uint8_t PIN_SPI_SCK   = 18;
constexpr uint8_t PIN_SPI_MISO  = 19;
constexpr uint8_t PIN_SPI_MOSI  = 23;
constexpr uint8_t PIN_LED_ACC   = 26;

MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
MFRC522::MIFARE_Key llaveFabrica;
Preferences prefs;

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedRFID    = io.feed("corto3-rfid");
AdafruitIO_Feed *feedRFIDCmd = io.feed("corto3-rfid-cmd");

// ---------------------- ESTADO DINAMICO ------------------------
std::vector<String> tarjetasAutorizadas;
bool modoLearn         = false;
bool modoLeerBloque    = false;
bool modoEscribirBloque = false;
String textoParaEscribir = "";
byte bloqueObjetivo    = 4; // Sector 1, bloque 4 (seguro; nunca escribir bloque 0 ni 3/7/11)

unsigned long cooldownRFIDMs   = 2000;
unsigned long ultimaLecturaMs  = 0;
unsigned long ultimoEnvioAioMs = 0;
String colaEventoAIO           = "";
String bufferSerial            = "";

// Normaliza UID a formato "AA:BB:CC:DD" (acepta con o sin ':', espacios o guiones)
String normalizarUID(String s) {
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

String uidAString(const MFRC522::Uid &uid) {
  String out = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) out += "0";
    out += String(uid.uidByte[i], HEX);
    if (i + 1 < uid.size) out += ":";
  }
  out.toUpperCase();
  return out;
}

int buscarUID(const String &uid) {
  for (size_t i = 0; i < tarjetasAutorizadas.size(); i++) {
    if (tarjetasAutorizadas[i] == uid) return (int)i;
  }
  return -1;
}

void guardarListaEnNVS() {
  String serializado = "";
  for (size_t i = 0; i < tarjetasAutorizadas.size(); i++) {
    if (i > 0) serializado += ",";
    serializado += tarjetasAutorizadas[i];
  }
  prefs.begin("rfid_db", false);
  prefs.putString("uids", serializado);
  prefs.end();
}

void cargarListaDeNVS() {
  tarjetasAutorizadas.clear();
  prefs.begin("rfid_db", true);
  String serializado = prefs.getString("uids", "");
  prefs.end();

  int inicio = 0;
  while (inicio < (int)serializado.length()) {
    int coma = serializado.indexOf(',', inicio);
    if (coma == -1) coma = serializado.length();
    String item = normalizarUID(serializado.substring(inicio, coma));
    if (item.length() >= 4) tarjetasAutorizadas.push_back(item);
    inicio = coma + 1;
  }
}

void agregarUID(String uid) {
  uid = normalizarUID(uid);
  if (uid.length() < 11) {
    Serial.println(F("[ERROR] UID invalido. Usa formato AA:BB:CC:DD o AABBCCDD."));
    return;
  }
  if (buscarUID(uid) == -1) {
    tarjetasAutorizadas.push_back(uid);
    guardarListaEnNVS();
    Serial.printf("[RFID DB] UID agregado: %s (Total=%u)\n",
                  uid.c_str(), (unsigned)tarjetasAutorizadas.size());
  } else {
    Serial.printf("[RFID DB] UID ya existia: %s\n", uid.c_str());
  }
}

void eliminarUID(String uid) {
  uid = normalizarUID(uid);
  if (uid.length() < 11) {
    Serial.println(F("[ERROR] UID invalido para eliminar."));
    return;
  }
  int idx = buscarUID(uid);
  if (idx >= 0) {
    tarjetasAutorizadas.erase(tarjetasAutorizadas.begin() + idx);
    guardarListaEnNVS();
    Serial.printf("[RFID DB] UID eliminado: %s\n", uid.c_str());
  } else {
    Serial.printf("[RFID DB] UID no encontrado: %s\n", uid.c_str());
  }
}

void imprimirLista() {
  Serial.printf("[RFID DB] Tarjetas autorizadas (%u):\n", (unsigned)tarjetasAutorizadas.size());
  for (size_t i = 0; i < tarjetasAutorizadas.size(); i++) {
    Serial.printf("  %u) %s\n", (unsigned)(i + 1), tarjetasAutorizadas[i].c_str());
  }
}

// Lee bloque MIFARE Classic (16 bytes) tras autenticar con llave A de fabrica
String leerBloqueMifare(byte bloque) {
  MFRC522::StatusCode status = rfid.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A, bloque, &llaveFabrica, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) {
    return "ERR_AUTH";
  }
  byte buffer[18];
  byte tam = sizeof(buffer);
  status = rfid.MIFARE_Read(bloque, buffer, &tam);
  if (status != MFRC522::STATUS_OK) {
    return "ERR_READ";
  }
  String txt = "";
  for (byte i = 0; i < 16; i++) {
    if (buffer[i] >= 32 && buffer[i] <= 126) txt += (char)buffer[i];
  }
  txt.trim();
  return txt;
}

bool escribirBloqueMifare(byte bloque, const String &dato) {
  // Proteccion: nunca escribir bloque 0 (UID fabricante) ni trailer de sector (3,7,11...)
  if (bloque == 0 || (bloque % 4) == 3) return false;

  MFRC522::StatusCode status = rfid.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A, bloque, &llaveFabrica, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) return false;

  byte buffer[16];
  memset(buffer, ' ', sizeof(buffer));
  size_t len = min((size_t)16, (size_t)dato.length());
  for (size_t i = 0; i < len; i++) {
    buffer[i] = (byte)dato.charAt(i);
  }
  status = rfid.MIFARE_Write(bloque, buffer, 16);
  return (status == MFRC522::STATUS_OK);
}

void ejecutarComando(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  String upper = cmd;
  upper.toUpperCase();

  if (upper == "LEARN") {
    modoLearn = true;
    Serial.println(F("[RFID] MODO LEARN ACTIVO: acerca la tarjeta a autorizar..."));
  } else if (upper.startsWith("ADD:")) {
    agregarUID(upper.substring(4));
  } else if (upper.startsWith("DEL:")) {
    eliminarUID(upper.substring(4));
  } else if (upper == "CLEAR") {
    tarjetasAutorizadas.clear();
    guardarListaEnNVS();
    Serial.println(F("[RFID DB] Lista borrada."));
  } else if (upper == "LIST") {
    imprimirLista();
  } else if (upper == "READ_BLOQUE") {
    modoLeerBloque = true;
    Serial.printf("[RFID] Acerca tarjeta para leer bloque %u...\n", bloqueObjetivo);
  } else if (upper.startsWith("WRITE_BLOQUE=")) {
    textoParaEscribir = cmd.substring(13); // Conserva mayusculas/minusculas originales
    modoEscribirBloque = true;
    Serial.printf("[RFID] Acerca tarjeta para escribir '%s' en bloque %u...\n",
                  textoParaEscribir.c_str(), bloqueObjetivo);
  } else if (upper == "CANCEL") {
    modoLearn = false;
    modoLeerBloque = false;
    modoEscribirBloque = false;
    Serial.println(F("[RFID] Operacion pendiente cancelada."));
  } else {
    Serial.println(F("Comandos: LEARN | ADD:AA:BB:CC:DD | DEL:AA:BB:CC:DD | LIST | CLEAR | READ_BLOQUE | WRITE_BLOQUE=TEXTO | CANCEL"));
  }
}

void alRecibirComandoAIO(AdafruitIO_Data *data) {
  ejecutarComando(data->toString());
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_ACC, OUTPUT);
  digitalWrite(PIN_LED_ACC, LOW);

  for (byte i = 0; i < 6; i++) llaveFabrica.keyByte[i] = 0xFF;

  cargarListaDeNVS();
  imprimirLista();

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  delay(50);
  byte ver = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("[RFID] Version chip: 0x%02X\n", ver);

  feedRFIDCmd->onMessage(alRecibirComandoAIO);
  Serial.print(F("[AIO] Conectando RFID + Adafruit IO"));
  io.connect();
  unsigned long inicio = millis();
  while (io.status() < AIO_CONNECTED && millis() - inicio < 15000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println(io.statusText());
}

void loop() {
  io.run(10, true);

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      ejecutarComando(bufferSerial);
      bufferSerial = "";
    } else if (bufferSerial.length() < 128) {
      bufferSerial += c;
    }
  }

  unsigned long ahora = millis();

  if (ahora - ultimaLecturaMs >= cooldownRFIDMs &&
      rfid.PICC_IsNewCardPresent() &&
      rfid.PICC_ReadCardSerial()) {
    ultimaLecturaMs = ahora;
    String uid = uidAString(rfid.uid);
    String evento;

    if (modoLearn) {
      modoLearn = false;
      agregarUID(uid);
      evento = "LEARN|" + uid;
      digitalWrite(PIN_LED_ACC, HIGH);
    } else if (modoEscribirBloque) {
      modoEscribirBloque = false;
      bool ok = escribirBloqueMifare(bloqueObjetivo, textoParaEscribir);
      evento = (ok ? "WRITE_OK|" : "WRITE_ERR|") + uid;
      Serial.println(ok ? F("[RFID] Escritura en bloque exitosa.") : F("[RFID] Fallo escritura en bloque."));
    } else if (modoLeerBloque) {
      modoLeerBloque = false;
      String contenido = leerBloqueMifare(bloqueObjetivo);
      evento = "DATA|" + contenido;
      Serial.printf("[RFID] Bloque %u = '%s'\n", bloqueObjetivo, contenido.c_str());
    } else {
      bool autorizado = (buscarUID(uid) >= 0);
      digitalWrite(PIN_LED_ACC, autorizado ? HIGH : LOW);
      evento = String(autorizado ? "OK|" : "DENIED|") + uid;
      Serial.printf("[RFID] %s -> %s\n", uid.c_str(), autorizado ? "AUTORIZADO" : "DENEGADO");
    }

    colaEventoAIO = evento;

    // Cierre limpio obligatorio de sesion RFID
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // Apaga LED de acceso tras 1.2s sin usar delay()
  if (digitalRead(PIN_LED_ACC) == HIGH && (ahora - ultimaLecturaMs >= 1200)) {
    digitalWrite(PIN_LED_ACC, LOW);
  }

  // Publicacion respetando rate limit de Adafruit IO
  if (colaEventoAIO.length() > 0 &&
      io.status() >= AIO_CONNECTED &&
      ahora - ultimoEnvioAioMs >= 2200) {
    ultimoEnvioAioMs = ahora;
    feedRFID->save(colaEventoAIO);
    Serial.print(F("[ESP32 -> AIO] "));
    Serial.println(colaEventoAIO);
    colaEventoAIO = "";
  }
}
