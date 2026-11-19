# Explicación de Comandos y Palabras Reservadas — `06_respaldo_wifimanager_oled.ino`

Plantilla **Plan B de laboratorio**: resuelve el escenario donde cambias de red WiFi en el labo (usando **WiFiManager**), quieres ver datos sin laptop (**OLED SH1106** como en `LCR.ino`), o quieres enviar múltiples datos en un solo payload **JSON** usando **Adafruit_MQTT_Library** directo.

---

## 1. Portal Cautivo (`WiFiManager`) y Credenciales Dinámicas

| Palabra / Comando | Qué hace en este código |
| :--- | :--- |
| `#include <WiFiManager.h>` | Permite que si el ESP32 no encuentra el WiFi guardado, levante automáticamente un punto de acceso temporal (`Corto3-ESP32-Setup`) con página web `192.168.4.1` para configurar WiFi y llaves desde el celular. |
| `WiFiManagerParameter` | Añade campos de formulario personalizados al portal cautivo web (`Adafruit IO Username` y `Adafruit IO Key`). |
| `wm.setSaveConfigCallback(...)` | Registra la función que avisa cuando el usuario presionó "Guardar" en la página del celular. |
| `wm.setConfigPortalTimeout(120)` | Evita que el portal bloquee para siempre si nadie entra en `120` segundos, permitiendo seguir funcionando en modo local. |
| `wm.autoConnect("Corto3-ESP32-Setup")` | Intenta conectarse al último WiFi guardado; si falla, abre el portal de configuración. |
| `wm.resetSettings()` + `ESP.restart()` | Borra la red WiFi guardada cuando escribes `RESET_WIFI` por Serial y reinicia el ESP32 para cambiar de red rápidamente. |

---

## 2. Pantalla OLED (`Adafruit_SH110X`), Sensores y Empaquetado `ArduinoJson`

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `Wire.begin(21, 22)` | Inicializa el bus I2C en los pines estándar del ESP32 (`SDA=21`, `SCL=22`). |
| `Adafruit_SH1106G display(...)` y `oledDisponible` | Controlador para OLED 1.3" SH1106. Si `display.begin(0x3C, true)` falla (OLED desconectada), `oledDisponible = false` evita reintentos I2C fallidos en `loop()`. |
| `display.clearDisplay()`, `setCursor()`, `print()`, `display()` | Limpia el buffer de pantalla, posiciona el texto, escribe las variables dinámicas y vuelca el buffer físico a la OLED. |
| `analogReadMilliVolts(PIN_LM35)` | Promedia 16 lecturas calibradas en `mV` para estabilidad térmica en la OLED. |
| `JsonDocument doc;` (`ArduinoJson v7`) | Crea un documento JSON dinámico en memoria. Permite empaquetar temperatura, IR y servo en **un solo envío** (`{"t":26.5,"ir":0,"srv":90}`), ahorrando cuota de Adafruit IO! |
| `serializeJson(doc, jsonOut)` / `roundf()` | Convierte el objeto JSON en texto `String` y redondea temperatura a 1 decimal. |

---

## 3. Cliente MQTT Directo (`Adafruit_MQTT_Client`) y NVS

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `WiFiClientSecure wifiTLS;` y `wifiTLS.setCACert(AIO_ROOT_CA)` | Abre un socket TLS seguro verificando el certificado raíz de `io.adafruit.com` en el puerto `8883`. |
| `Adafruit_MQTT_Client` / `new` / `nullptr` | Cliente MQTT puro instanciado dinámicamente con `new` tras cargar credenciales (`nullptr` indica puntero aún vacío antes de `setup()`). |
| `Adafruit_MQTT_Publish` / `Adafruit_MQTT_Subscribe` | Objetos para publicar (`publish()`) o suscribirse (`readSubscription()`) manualmente a tópicos `<usuario>/feeds/<nombre-feed>`. |
| `mqtt->connected()` / `mqtt->connect()` / `mqtt->disconnect()` / `connectErrorString()` | Gestionan la conexión y reconexión no bloqueante con backoff de `60 s` si hay error de clave o *throttle*. |
| `mqtt->ping()` | Envía un `PINGREQ` cada `60 s` cuando está *idle* (o con `PAUSE_PUB` activo) para que el broker no cierre la sesión por inactividad. |
| `strtof` / `roundf` / `constrain` (`parseFloatSeguro`) | Valida que el ángulo recibido por MQTT sea un número válido antes de mover el servo. |
| `Preferences` / `strlcpy()` / `toCharArray()` | Guarda y recupera `aioUser` y `aioKey` en Flash NVS copiando cadenas con límite seguro de buffer. |
| `PAUSE_PUB` / `STATUS` | Comandos no bloqueantes por Serial para pausar/reanudar envíos MQTT (probando keep-alive) o consultar estado. |
