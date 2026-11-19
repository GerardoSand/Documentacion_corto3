# Explicación de Comandos y Palabras Reservadas — `02b_ir_control_remoto_adafruit.ino`

Código funcional para decodificar **cualquier control remoto infrarrojo** (protocolos NEC, Samsung, Sony, LG, etc.) usando `IRremote v4.x`, aprender botones dinámicamente en memoria persistente (`Preferences`) y enviar el evento a **Adafruit IO**.

---

## 1. Librerías y Directivas Especiales

| Palabra / Comando | Qué hace en este código |
| :--- | :--- |
| `#define NO_LED_FEEDBACK_CODE` | Debe declararse **antes** de `#include <IRremote.hpp>`. Desactiva el parpadeo automático de `LED_BUILTIN` (`GPIO 2`) para evitar interferencias con otros pines. |
| `#include <IRremote.hpp>` | Incluye la librería moderna `IRremote v4.x` (`.hpp` en lugar del antiguo `.h`). |
| `#include <Preferences.h>` | Librería nativa del ESP32 para guardar variables en memoria **NVS (Flash)** y que no se pierdan al desconectar la corriente. |

---

## 2. Objetos y Métodos de `IRremote` v4.x

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `IrReceiver.begin(PIN_IR_RX, DISABLE_LED_FEEDBACK)` | Inicia la interrupción del receptor IR en `GPIO 27`. `DISABLE_LED_FEEDBACK` (`false`) desactiva el parpadeo de LED por recepción. |
| `IrReceiver.decode()` | Devuelve `true` si llegó y decodificó una trama infrarroja completa. |
| `IrReceiver.decodedIRData` | Estructura donde se guarda el último paquete recibido (`.protocol`, `.address`, `.command`, `.flags`). |
| `IRDATA_FLAGS_IS_REPEAT` y `&` (bit a bit) | Máscara de bits evaluada con el operador AND bit a bit (`flags & IRDATA_FLAGS_IS_REPEAT`) para saber si el usuario dejó presionado el botón y evitar disparar acciones repetidas. |
| `uint16_t` / `uint8_t` | Enteros sin signo de 16 bits (`0..65535`, usado para códigos IR `cmdEncender`/`cmdApagar`) y de 8 bits (`0..255`, para pines y estados). |
| `String(comando, HEX)` | Convierte el número `comando` a texto en base hexadecimal para publicarlo legible en Adafruit IO (ej. `"ON(0x45)"`). |
| `decode_type_t` / `UNKNOWN` | Tipo enumerado del protocolo detectado (`NEC`, `SONY`, `SAMSUNG`, `UNKNOWN` si fue ruido). |
| `getProtocolString(proto)` | Convierte el código de protocolo a texto legible (`"NEC"`, `"LG"`, etc.). |
| `IrReceiver.resume()` | Libera el buffer y reactiva la escucha del receptor para el siguiente pulso. **Si la olvidas, solo leerá 1 vez y dejará de responder.** |

---

## 3. Persistencia Dinámica en Flash (`Preferences`) y Red

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `prefs.begin("ir_cfg", false)` | Abre el espacio de nombres `"ir_cfg"` en Flash (`false` = lectura/escritura, `true` = solo lectura). |
| `prefs.putUShort("cmd_on", val)` | Guarda un entero sin signo de 16 bits (`uint16_t`) bajo la clave `"cmd_on"`. |
| `prefs.getUShort("cmd_on", 0xFFFF)` | Lee la clave guardada; si nunca se ha guardado nada, devuelve `0xFFFF`. |
| `prefs.end()` | Cierra el manejador de NVS de forma segura tras leer o escribir. |
| `alRecibirComando` / `BORRAR` | Escucha `"APRENDER_ON"`, `"APRENDER_OFF"` y `"BORRAR"` desde el feed `"corto3-rfid-cmd"` o Monitor Serial sin bloquear `loop()`. |
