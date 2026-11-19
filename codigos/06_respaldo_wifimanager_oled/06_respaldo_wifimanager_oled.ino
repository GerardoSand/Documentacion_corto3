/*
 * ============================================================================
 * SKETCH 06: PLAN B DE LABORATORIO (WiFiManager + OLED SH1106 + MQTT/JSON)
 * ============================================================================
 * Para usar cuando:
 *  - No sabes el SSID/Password del laboratorio (abre Portal Cautivo desde el cel!)
 *  - Necesitas ver datos en una OLED SH1106 (igual que LCR.ino) sin laptop
 *  - Quieres un cliente MQTT alternativo directo con Adafruit_MQTT + ArduinoJson
 *
 * Hardware:
 *  - OLED SH1106 I2C -> SDA=GPIO 21, SCL=GPIO 22, VCC=3.3V (dir 0x3C)
 *  - LM35 OUT        -> GPIO 34 (ADC1)
 *  - IR DO           -> GPIO 32
 *  - Servo           -> GPIO 25
 *
 * Portal Cautivo WiFiManager:
 *  - Si no encuentra WiFi guardado, crea la red "Corto3-ESP32-Setup" (192.168.4.1)
 *  - Desde ahi configuras WiFi, IO_USERNAME e IO_KEY y se guardan en NVS Flash!
 *  - Envia "RESET_WIFI" por Serial para borrar credenciales y reabrir el portal.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ---------------------- OLED SH1106 (como LCR.ino) ---------------
constexpr uint8_t I2C_ADDR_OLED = 0x3C;
constexpr int SCREEN_W = 128;
constexpr int SCREEN_H = 64;
Adafruit_SH1106G display(SCREEN_W, SCREEN_H, &Wire, -1);

// ---------------------- PINES ------------------------------------
constexpr uint8_t PIN_LM35  = 34;
constexpr uint8_t PIN_IR_DO = 32;
constexpr uint8_t PIN_SERVO = 25;

// ---------------------- CREDENCIALES DINAMICAS NVS ---------------
Preferences prefs;
char aioUser[48] = "TU_USUARIO_ADAFRUIT";
char aioKey[64]  = "aio_TU_LLAVE_SECRETA_AQUI";
bool debeGuardarParams = false;

// Certificado Root CA DigiCert Global Root G2 usado por io.adafruit.com
const char *AIO_ROOT_CA =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIEjTCCA3WgAwIBAgIQDQd4KhM/xvmlcpbhMf/ReTANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xNzExMDIxMjIzMzdaFw0yNzExMDIxMjIzMzdaMGAxCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xHzAdBgNVBAMTFkdlb1RydXN0IFRMUyBSU0EgQ0EgRzEwggEiMA0GCSqGSIb3\n"
    "DQEBAQUAA4IBDwAwggEKAoIBAQC+F+jsvikKy/65LWEx/TMkCDIuWegh1Ngwvm4Q\n"
    "yISgP7oU5d79eoySG3vOhC3w/3jEMuipoH1fBtp7m0tTpsYbAhch4XA7rfuD6whU\n"
    "gajeErLVxoiWMPkC/DnUvbgi74BJmdBiuGHQSd7LwsuXpTEGG9fYXcbTVN5SATYq\n"
    "DfbexbYxTMwVJWoVb6lrBEgM3gBBqiiAiy800xu1Nq07JdCIQkBsNpFtZbIZhsDS\n"
    "fzlGWP4wEmBQ3O67c+ZXkFr2DcrXBEtHam80Gp2SNhou2U5U7UesDL/xgLK6/0d7\n"
    "6TnEVMSUVJkZ8VeZr+IUIlvoLrtjLbqugb0T3OYXW+CQU0kBAgMBAAGjggFAMIIB\n"
    "PDAdBgNVHQ4EFgQUlE/UXYvkpOKmgP792PkA76O+AlcwHwYDVR0jBBgwFoAUTiJU\n"
    "IBiV5uNu5g/6+rkS7QYXjzkwDgYDVR0PAQH/BAQDAgGGMB0GA1UdJQQWMBQGCCsG\n"
    "AQUFBwMBBggrBgEFBQcDAjASBgNVHRMBAf8ECDAGAQH/AgEAMDQGCCsGAQUFBwEB\n"
    "BCgwJjAkBggrBgEFBQcwAYYYaHR0cDovL29jc3AuZGlnaWNlcnQuY29tMEIGA1Ud\n"
    "HwQ7MDkwN6A1oDOGMWh0dHA6Ly9jcmwzLmRpZ2ljZXJ0LmNvbS9EaWdpQ2VydEds\n"
    "b2JhbFJvb3RHMi5jcmwwPQYDVR0gBDYwNDAyBgRVHSAAMCowKAYIKwYBBQUHAgEW\n"
    "HGh0dHBzOi8vd3d3LmRpZ2ljZXJ0LmNvbS9DUFMwDQYJKoZIhvcNAQELBQADggEB\n"
    "AIIcBDqC6cWpyGUSXAjjAcYwsK4iiGF7KweG97i1RJz1kwZhRoo6orU1JtBYnjzB\n"
    "c4+/sXmnHJk3mlPyL1xuIAt9sMeC7+vreRIF5wFBC0MCN5sbHwhNN1JzKbifNeP5\n"
    "ozpZdQFmkCo+neBiKR6HqIA+LMTMCMMuv2khGGuPHmtDze4GmEGZtYLyF8EQpa5Y\n"
    "jPuV6k2Cr/N3XxFpT3hRpt/3usU/Zb9wfKPtWpoznZ4/44c1p9rzFcZYrWkj3A+7\n"
    "TNBJE0GmP2fhXhP1D/XVfIW/h0yCJGEiV9Glm/uGOa3DXHlmbAcxSyCRraG+ZBkA\n"
    "7h4SeM6Y8l/7MBRpPCz6l8Y=\n"
    "-----END CERTIFICATE-----\n";

WiFiClientSecure wifiTLS;
Adafruit_MQTT_Client *mqtt = nullptr;
Adafruit_MQTT_Publish *pubResumen = nullptr;
Adafruit_MQTT_Subscribe *subServo = nullptr;
String topicPub = "";
String topicSub = "";

Servo servoPlanB;
int anguloServo = 90;
float tempC = 0.0f;
bool irDet = false;
bool oledDisponible = false;
bool publicacionPausada = false;

unsigned long ultimoMuestreoMs = 0;
unsigned long ultimoOledMs     = 0;
unsigned long ultimoMqttMs     = 0;
unsigned long ultimoPingMs     = 0;
unsigned long ultimoIntentoMqtt = 0;
unsigned long intervaloReintentoMqttMs = 5000;
String bufferSerial = "";

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

bool credencialesAIOValidas() {
  return strlen(aioUser) > 0 &&
         strcmp(aioUser, "TU_USUARIO_ADAFRUIT") != 0 &&
         strlen(aioKey) > 0 &&
         strcmp(aioKey, "aio_TU_LLAVE_SECRETA_AQUI") != 0;
}

void callbackGuardarConfig() {
  debeGuardarParams = true;
}

void cargarCredenciales() {
  prefs.begin("aio_cfg", true);
  String u = prefs.getString("user", "");
  String k = prefs.getString("key", "");
  prefs.end();
  if (u.length() > 0) u.toCharArray(aioUser, sizeof(aioUser));
  if (k.length() > 0) k.toCharArray(aioKey, sizeof(aioKey));
}

void guardarCredenciales() {
  prefs.begin("aio_cfg", false);
  prefs.putString("user", aioUser);
  prefs.putString("key", aioKey);
  prefs.end();
}

void dibujarOLED() {
  if (!oledDisponible) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(0, 0);
  display.print("CORTO 3 | ");
  display.println(WiFi.status() == WL_CONNECTED ? "WIFI OK" : "SIN WIFI");

  display.setCursor(0, 14);
  display.print("Temp: ");
  display.print(tempC, 1);
  display.println(" C");

  display.setCursor(0, 26);
  display.print("IR: ");
  display.println(irDet ? "DETECTADO" : "LIBRE");

  display.setCursor(0, 38);
  display.print("Servo: ");
  display.print(anguloServo);
  display.println(" deg");

  display.setCursor(0, 52);
  display.print("MQTT: ");
  display.println((mqtt && mqtt->connected()) ? "CONECTADO" : "OFFLINE");

  display.display();
}

void conectarMQTTNoBloqueante(unsigned long ahora) {
  if (!mqtt || mqtt->connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (!credencialesAIOValidas()) return;
  if (ultimoIntentoMqtt != 0 && (ahora - ultimoIntentoMqtt < intervaloReintentoMqttMs)) return;

  ultimoIntentoMqtt = ahora;
  int8_t ret = mqtt->connect();
  if (ret != 0) {
    mqtt->disconnect();
    // Backoff de 60 s en errores de autenticacion (4/5) o throttle/ban (6/7)
    intervaloReintentoMqttMs = (ret >= 4 && ret <= 7) ? 60000UL : 5000UL;
    Serial.printf("[MQTT] Error %d: %s\n", ret, mqtt->connectErrorString(ret));
  } else {
    intervaloReintentoMqttMs = 5000UL;
    ultimoPingMs = ahora;
    Serial.println(F("[MQTT] Conectado directo a io.adafruit.com:8883!"));
  }
}

void procesarSerialPlanB(String s) {
  s.trim();
  s.toUpperCase();
  if (s.length() == 0) return;

  if (s == "RESET_WIFI") {
    WiFiManager wm;
    wm.resetSettings();
    Serial.println(F("[WM] Credenciales borradas. Reiniciando..."));
    delay(500);
    ESP.restart();
  } else if (s == "PAUSE_PUB") {
    publicacionPausada = !publicacionPausada;
    Serial.println(publicacionPausada ? F("[MQTT] Publicacion pausada (ping keep-alive activo).") : F("[MQTT] Publicacion reanudada."));
  } else if (s == "STATUS") {
    Serial.printf("[STATUS] WiFi=%s | MQTT=%s | OLED=%s | T=%.1fC | IR=%d | Servo=%d\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "OFF",
                  (mqtt && mqtt->connected()) ? "OK" : "OFF",
                  oledDisponible ? "OK" : "NO",
                  tempC, irDet ? 1 : 0, anguloServo);
  } else {
    Serial.println(F("Comandos: RESET_WIFI | PAUSE_PUB | STATUS"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_IR_DO, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LM35, ADC_11db);

  ESP32PWM::allocateTimer(0);
  servoPlanB.setPeriodHertz(50);
  servoPlanB.attach(PIN_SERVO, 500, 2400);
  servoPlanB.write(anguloServo);

  Wire.begin(21, 22);
  oledDisponible = display.begin(I2C_ADDR_OLED, true);
  if (oledDisponible) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("Iniciando WiFiMgr...");
    display.println("Red: Corto3-ESP32");
    display.display();
  } else {
    Serial.println(F("[OLED] No encontrada en 0x3C (continuando sin OLED)."));
  }

  cargarCredenciales();

  WiFiManager wm;
  wm.setSaveConfigCallback(callbackGuardarConfig);
  wm.setConfigPortalTimeout(120); // 2 min timeout para no quedar colgado si no configuran

  WiFiManagerParameter pUser("aio_user", "Adafruit IO Username", aioUser, sizeof(aioUser) - 1);
  WiFiManagerParameter pKey("aio_key", "Adafruit IO Key", aioKey, sizeof(aioKey) - 1);
  wm.addParameter(&pUser);
  wm.addParameter(&pKey);

  if (!wm.autoConnect("Corto3-ESP32-Setup")) {
    Serial.println(F("[WM] No se conecto WiFi en el portal (modo local activo)."));
  }

  if (debeGuardarParams) {
    strlcpy(aioUser, pUser.getValue(), sizeof(aioUser));
    strlcpy(aioKey, pKey.getValue(), sizeof(aioKey));
    guardarCredenciales();
  }

  wifiTLS.setCACert(AIO_ROOT_CA);
  topicPub = String(aioUser) + "/feeds/corto3-rfid";
  topicSub = String(aioUser) + "/feeds/corto3-servo-cmd";

  mqtt = new Adafruit_MQTT_Client(&wifiTLS, "io.adafruit.com", 8883, aioUser, aioKey);
  pubResumen = new Adafruit_MQTT_Publish(mqtt, topicPub.c_str());
  subServo   = new Adafruit_MQTT_Subscribe(mqtt, topicSub.c_str());
  mqtt->subscribe(subServo);
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      procesarSerialPlanB(bufferSerial);
      bufferSerial = "";
    } else if (bufferSerial.length() < 128) {
      bufferSerial += c;
    }
  }

  unsigned long ahora = millis();
  conectarMQTTNoBloqueante(ahora);

  if (mqtt && mqtt->connected()) {
    Adafruit_MQTT_Subscribe *sub;
    while ((sub = mqtt->readSubscription(10))) {
      if (sub == subServo) {
        float valF = 0.0f;
        if (parseFloatSeguro(String((char *)subServo->lastread), valF)) {
          anguloServo = (int)constrain((long)roundf(valF), 0L, 180L);
          servoPlanB.write(anguloServo);
          Serial.printf("[MQTT ->] Nuevo angulo servo = %d\n", anguloServo);
        }
      }
    }

    // Keep-alive MQTT cuando pasa idle o se pausa la publicacion (cada 60 s)
    if (ahora - ultimoPingMs >= 60000) {
      ultimoPingMs = ahora;
      if (!mqtt->ping()) {
        Serial.println(F("[MQTT] Keep-alive ping fallo, reconectando..."));
        mqtt->disconnect();
      }
    }
  }

  if (ahora - ultimoMuestreoMs >= 500) {
    ultimoMuestreoMs = ahora;
    long sumaMv = 0;
    for (int i = 0; i < 16; i++) {
      sumaMv += analogReadMilliVolts(PIN_LM35);
    }
    tempC = (sumaMv / 16.0f) / 10.0f;
    irDet = (digitalRead(PIN_IR_DO) == LOW);
  }

  if (ahora - ultimoOledMs >= 500) {
    ultimoOledMs = ahora;
    dibujarOLED();
  }

  // Envia resumen JSON compacto cada 4 segundos usando ArduinoJson v7
  if (!publicacionPausada && mqtt && mqtt->connected() && (ahora - ultimoMqttMs >= 4000)) {
    ultimoMqttMs = ahora;

    JsonDocument doc;
    doc["t"] = roundf(tempC * 10.0f) / 10.0f;
    doc["ir"] = irDet ? 1 : 0;
    doc["srv"] = anguloServo;

    String jsonOut;
    serializeJson(doc, jsonOut);
    if (pubResumen->publish(jsonOut.c_str())) {
      ultimoPingMs = ahora;
      Serial.printf("[MQTT ->] %s\n", jsonOut.c_str());
    } else {
      Serial.println(F("[MQTT] Fallo publish(), desconectando para reintentar..."));
      mqtt->disconnect();
    }
  }
}
