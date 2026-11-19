/*
 * ============================================================================
 * SKETCH 02b: RECEPTOR IR CONTROL REMOTO (NEC/SONY/SAMSUNG) + ADAFRUIT IO
 * ============================================================================
 * Usar SOLO si en el Corto 3 les entregan un receptor IR de control remoto
 * (VS1838B / KY-022 de 3 patas) y un control remoto fisico.
 *
 * Hardware:
 *  - Receptor IR: VCC -> 3.3V | GND -> GND | OUT/S -> GPIO 27
 *  - LED indicador opcional -> GPIO 26
 *
 * Capacidad Dinamica ("TEACH MODE" - Sin hardcodear codigos del control!):
 *  - Envia "APRENDER_ON" o "APRENDER_OFF" desde Adafruit IO ("corto3-rfid-cmd")
 *    o por Serial, presiona cualquier tecla del control y el ESP32 la guarda
 *    en memoria flash NVS (Preferences) para siempre!
 *
 * Feeds Adafruit IO:
 *  - "corto3-ir"       (ESP32 -> Dashboard): Ultimo boton / accion decodificada
 *  - "corto3-rfid-cmd" (Dashboard -> ESP32): "APRENDER_ON", "APRENDER_OFF"
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

// Opciones de IRremote v4.x ANTES de incluir <IRremote.hpp>
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     "TU_WIFI_SSID"
  #define WIFI_PASS     "TU_WIFI_PASSWORD"
  #define IO_USERNAME   "TU_USUARIO_ADAFRUIT"
  #define IO_KEY        "aio_TU_LLAVE_SECRETA_AQUI"
#endif

#include "AdafruitIO_WiFi.h"

constexpr uint8_t PIN_IR_RX  = 27; // Receptor VS1838B / KY-022
constexpr uint8_t PIN_LED_IR = 26;

Preferences prefs;
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedIR  = io.feed("corto3-ir");
AdafruitIO_Feed *feedCmd = io.feed("corto3-rfid-cmd");

// Comandos aprendidos dinamicamente (0xFFFF = aun no asignado)
uint16_t cmdEncender = 0xFFFF;
uint16_t cmdApagar   = 0xFFFF;
uint8_t  modoAprender = 0; // 0=normal, 1=esperando tecla ON, 2=esperando tecla OFF
bool     estadoSalida = false;

unsigned long ultimoEnvioMs = 0;
String colaMensajeAIO = "";
String bufferSerial   = "";

void guardarCodigosEnFlash() {
  prefs.begin("ir_cfg", false);
  prefs.putUShort("cmd_on", cmdEncender);
  prefs.putUShort("cmd_off", cmdApagar);
  prefs.end();
}

void cargarCodigosDeFlash() {
  prefs.begin("ir_cfg", true);
  cmdEncender = prefs.getUShort("cmd_on", 0xFFFF);
  cmdApagar   = prefs.getUShort("cmd_off", 0xFFFF);
  prefs.end();
}

void activarModoAprendizaje(uint8_t tipo) {
  modoAprender = tipo;
  Serial.printf("[IR TEACH] Presiona ahora en tu control el boton para %s...\n",
                tipo == 1 ? "ENCENDER" : "APAGAR");
}

void alRecibirComando(AdafruitIO_Data *data) {
  String cmd = data->toString();
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "APRENDER_ON") activarModoAprendizaje(1);
  else if (cmd == "APRENDER_OFF") activarModoAprendizaje(2);
  else if (cmd == "BORRAR") {
    cmdEncender = 0xFFFF;
    cmdApagar = 0xFFFF;
    guardarCodigosEnFlash();
    Serial.println(F("[AIO -> ESP32] Codigos IR borrados."));
  }
}

void procesarSerial(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "APRENDER_ON") activarModoAprendizaje(1);
  else if (cmd == "APRENDER_OFF") activarModoAprendizaje(2);
  else if (cmd == "BORRAR") {
    cmdEncender = 0xFFFF;
    cmdApagar = 0xFFFF;
    guardarCodigosEnFlash();
    Serial.println(F("[IR] Codigos borrados."));
  } else if (cmd.length() > 0) {
    Serial.println(F("Comandos: APRENDER_ON | APRENDER_OFF | BORRAR"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_IR, OUTPUT);
  digitalWrite(PIN_LED_IR, LOW);

  cargarCodigosDeFlash();
  Serial.printf("[IR NVS] Cargados ON=0x%04X | OFF=0x%04X\n", cmdEncender, cmdApagar);

  IrReceiver.begin(PIN_IR_RX, DISABLE_LED_FEEDBACK);
  Serial.printf("[IR] Receptor listo en GPIO %u\n", PIN_IR_RX);

  feedCmd->onMessage(alRecibirComando);
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
      procesarSerial(bufferSerial);
      bufferSerial = "";
    } else if (bufferSerial.length() < 128) {
      bufferSerial += c;
    }
  }

  if (IrReceiver.decode()) {
    bool esRepeticion = (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT);
    uint16_t comando  = IrReceiver.decodedIRData.command;
    decode_type_t proto = IrReceiver.decodedIRData.protocol;

    if (!esRepeticion && proto != UNKNOWN) {
      Serial.printf("[IR RX] Proto=%s | Addr=0x%X | Cmd=0x%X\n",
                    getProtocolString(proto),
                    IrReceiver.decodedIRData.address,
                    comando);

      if (modoAprender == 1) {
        cmdEncender = comando;
        guardarCodigosEnFlash();
        modoAprender = 0;
        colaMensajeAIO = "LEARN_ON:0x" + String(comando, HEX);
      } else if (modoAprender == 2) {
        cmdApagar = comando;
        guardarCodigosEnFlash();
        modoAprender = 0;
        colaMensajeAIO = "LEARN_OFF:0x" + String(comando, HEX);
      } else {
        String accion = "CMD:0x" + String(comando, HEX);
        if (comando == cmdEncender) {
          estadoSalida = true;
          digitalWrite(PIN_LED_IR, HIGH);
          accion = "ON(0x" + String(comando, HEX) + ")";
        } else if (comando == cmdApagar) {
          estadoSalida = false;
          digitalWrite(PIN_LED_IR, LOW);
          accion = "OFF(0x" + String(comando, HEX) + ")";
        }
        colaMensajeAIO = accion;
      }
    }
    IrReceiver.resume(); // Imprescindible para volver a escuchar el siguiente pulso
  }

  // Envia al Dashboard respetando limite de 1 mensaje cada >= 2200 ms
  if (colaMensajeAIO.length() > 0 &&
      io.status() >= AIO_CONNECTED &&
      millis() - ultimoEnvioMs >= 2200) {
    ultimoEnvioMs = millis();
    feedIR->save(colaMensajeAIO);
    Serial.print(F("[ESP32 -> AIO] "));
    Serial.println(colaMensajeAIO);
    colaMensajeAIO = "";
  }
}
