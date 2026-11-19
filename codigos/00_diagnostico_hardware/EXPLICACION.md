# Explicación de Comandos y Palabras Reservadas — `00_diagnostico_hardware.ino`

Este código sirve para **verificar en segundos** si tus pines, sensores, servomotor, lector RFID y credenciales de Adafruit IO responden bien antes de empezar a programar la lógica del examen.

---

## 1. Directivas de Preprocesador y Librerías

| Palabra / Comando | Qué hace exactamente en este código |
| :--- | :--- |
| `#include <Arduino.h>` | Incluye las funciones base de Arduino (`pinMode`, `digitalRead`, `millis`, `Serial`, tipos `byte`, `String`). |
| `#include <SPI.h>` | Habilita el bus de comunicación serial síncrona **SPI** necesario para hablar con el lector `MFRC522`. |
| `#include <MFRC522.h>` | Carga la librería del chip RFID RC522 (lectura de tarjetas ISO-14443A). |
| `#include <ESP32Servo.h>` | Carga la librería de servomotores adaptada a los timers PWM hardware del ESP32. |
| `#include <WiFi.h>` | Controla el radio WiFi de 2.4 GHz del ESP32 (escaneo de redes y estado de conexión). |
| `#include "AdafruitIO_WiFi.h"` | Incluye el cliente oficial de Adafruit IO que selecciona `AdafruitIO_ESP32` automáticamente. |
| `#if __has_include("secrets.h")` | Directiva condicional de C++17: revisa en tiempo de compilación si existe `secrets.h` dentro de la carpeta del sketch. Si existe, lo usa; si no, usa las constantes `#else` para que el código **siempre compile**. |
| `#define` | Crea constantes textuales en compilación (`WIFI_SSID`, `IO_KEY`, etc.) sin ocupar RAM extra. |
| `constexpr uint8_t` | Declara constantes enteras de 8 bits sin signo (`0..255`) evaluadas en compilación con verificación estricta de tipo (mejor práctica C++ que `#define` para pines). |
| `void` / `bool` / `int` / `byte` / `unsigned long` / `const` / `String` | Tipos y calificadores C++/Arduino: `void` (sin retorno), `bool` (`true`/`false`), `int` (entero con signo), `byte` (`0..255`), `unsigned long` (entero de 32 bits sin signo para `millis()`), `const` (inmutable) y `String` (cadenas dinámicas). |
| `setup()` y `loop()` | Funciones obligatorias de Arduino: `setup()` corre una vez al arrancar y `loop()` se repite continuamente sin bloqueos. |

---

## 2. Objetos y Configuración de Hardware

| Comando / Palabra | Qué hace |
| :--- | :--- |
| `AdafruitIO_WiFi io(...)` | Crea el cliente de Adafruit IO para ESP32 usando TLS (puerto 8883) con usuario, llave secreta, SSID y contraseña WiFi. |
| `AdafruitIO_Feed *feedEstado = io.feed("corto3-rfid")` | Crea un puntero a un feed llamado `"corto3-rfid"` para poder enviar mensajes de prueba (`save`). |
| `MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST)` | Instancia el controlador RFID indicando cuál pin activa el chip (`SS/SDA = 5`) y cuál lo reinicia (`RST = 4`). |
| `Servo servoTest` | Instancia un objeto servo de `ESP32Servo`. |
| `Serial.begin(115200)` | Inicializa el puerto serie USB a `115200` baudios (velocidad estándar del ESP32 para no ver caracteres raros). |
| `F("texto")` | Macro de Arduino (`FlashStringHelper`) que evita copiar cadenas constantes innecesariamente en buffers temporales. |
| `analogReadResolution(12)` | Fija el ADC a 12 bits de resolución (`0` a `4095`). |
| `analogSetPinAttenuation(pin, ADC_11db)` | Configura la atenuación del pin analógico a `11 dB` para medir hasta ~`3.1V–3.3V` sin saturar. |
| `pinMode(PIN_IR_DO, INPUT)` | Configura el pin digital del sensor IR como entrada. |
| `ESP32PWM::allocateTimer(0)` | Reserva explícitamente el timer PWM hardware #0 del ESP32 para generar pulsos estables de servo. |
| `servoTest.setPeriodHertz(50)` | Ajusta la frecuencia PWM a `50 Hz` (un pulso cada `20 ms`, estándar de servos SG90/MG995). |
| `servoTest.attach(PIN_SERVO, 500, 2400)` | Conecta el servo al `GPIO 25` definiendo pulso mínimo `500 µs` (`0°`) y máximo `2400 µs` (`180°`). |
| `SPI.begin(SCK, MISO, MOSI, SS)` | Inicia el bus VSPI del ESP32 con los pines exactos del mapa seguro (`18, 19, 23, 5`). |
| `rfid.PCD_Init()` | Inicializa el chip MFRC522, enciende su antena de 13.56 MHz y configura ganancia por defecto. |

---

## 3. Funciones de Diagnóstico, Red y Tiempo Real

| Comando / Palabra | Qué hace |
| :--- | :--- |
| `rfid.PCD_ReadRegister(MFRC522::VersionReg)` | Lee el registro interno de versión de firmware del chip RC522. Devuelve `0x91`, `0x92` o `0x88` si está bien cableado; `0x00` o `0xFF` si hay un cable suelto o invertido. |
| `io.connect()` | Inicia la conexión WiFi y prepara el certificado raíz SSL para conectar por MQTT a `io.adafruit.com`. |
| `io.status()` y `AIO_CONNECTED` | `io.status()` devuelve un código `aio_status_t`; mientras sea menor que `AIO_CONNECTED` (`21`), aún está conectando WiFi o MQTT. |
| `io.statusText()` | Convierte el estado numérico actual de Adafruit IO en un mensaje legible en español/inglés para diagnosticar errores de clave (`Authentication failed`) o WiFi. |
| `io.run(10, true)` | Procesa paquetes MQTT de Adafruit IO esperando máximo `10 ms` y con `fail_fast = true` (si no hay internet, **no congela** el resto de sensores locales). |
| `millis()` | Devuelve los milisegundos transcurridos desde que arrancó el ESP32 (`unsigned long`). Base de toda temporización sin `delay()`. |
| `analogReadMilliVolts(pin)` | Lee el pin analógico usando la **calibración de fábrica (eFuse)** del ESP32 y devuelve directamente milivoltios (`mV`), eliminando la no-linealidad del ADC crudo. |
| `digitalRead(PIN_IR_DO)` | Lee `HIGH` (`1`) o `LOW` (`0`) en la salida digital del módulo infrarrojo. |
| `rfid.PICC_IsNewCardPresent()` | Pregunta por radiofrecuencia si hay una tarjeta nueva cerca de la antena. |
| `rfid.PICC_ReadCardSerial()` | Lee el identificador único (`UID`) de esa tarjeta y lo guarda en `rfid.uid`. |
| `rfid.PICC_HaltA()` y `rfid.PCD_StopCrypto1()` | Pone la tarjeta en estado de reposo y cierra la sesión criptográfica para poder detectar nuevas lecturas limpiamente después. |
| `WiFi.scanNetworks()` / `WiFi.scanDelete()` | Lista las redes WiFi de `2.4 GHz` visibles (útil para saber el nombre exacto de la red del labo) y libera la memoria usada por el escaneo. |
| `Serial.available()` / `Serial.read()` | Permite leer comandos escritos por teclado en el Monitor Serial sin bloquear la ejecución del `loop()`. |
| `Serial.printf(...)` | Imprime texto formateado estilo C (`%d` entero, `%.2f` decimal con 2 cifras, `%s` cadena `const char*`). |
| `cmd.trim()` / `cmd.toUpperCase()` | Limpia espacios/saltos de línea al inicio y final del comando y lo pasa a mayúsculas para compararlo sin errores de formato. |
| `delay(ms)` | Pausa bloqueante usada **solo en `setup()`** (arranque del chip y espera inicial de WiFi); prohibida en `loop()`. |
| `WiFi.mode(WIFI_STA)` / `WL_CONNECTED` | Pone el ESP32 en modo estación (cliente WiFi) cuando no hay credenciales editadas y verifica si ya tiene IP asignada. |
| `feedEstado->save(String("DIAGNOSTICO_OK"))` | Envía explícitamente un `String` (se evita literal `const char*` directo porque `AdafruitIO_Feed::save` lo convertiría a `bool`). |
| `return;` | Sale inmediatamente de la función actual (por ejemplo, si el comando recibido está vacío). |
