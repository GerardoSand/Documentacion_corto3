# Explicación de Comandos y Palabras Reservadas — `01_lm35_adafruit.ino`

Código funcional para medir temperatura con **LM35** de forma estable en **ESP32 con WiFi encendido**, aceptando calibración, pines ADC1, unidades y umbrales dinámicos desde **Adafruit IO** y **Monitor Serial**.

---

## 1. Tipos de Datos, Calificadores y Variables Dinámicas

| Palabra / Comando | Qué hace en este código |
| :--- | :--- |
| `uint8_t` | Entero sin signo de 8 bits (`0` a `255`), ideal para números de pin GPIO ahorrando memoria. |
| `uint32_t` | Entero sin signo de 32 bits (`0` a `4,294,967,295`), usado por `analogReadMilliVolts`. |
| `long` / `unsigned long` | Enteros de 32 bits con signo (`long`, usado en sumatorias ADC y `strtol`) y sin signo (`unsigned long`, obligatorio para comparar tiempos con `millis()`). |
| `float` | Número decimal de coma flotante (32 bits), necesario para temperaturas con decimales (`25.43 °C`). |
| `bool` | Booleano (`true` o `false`) para guardar si `alertaActiva` está encendida o apagada. |
| `char` | Carácter individual (`'C'`, `'F'`, `'K'`) para cambiar dinámicamente la escala de temperatura. |
| `String` | Clase de texto de Arduino para recibir y analizar comandos del Monitor Serial o Dashboard. |
| `constexpr` | Declara valores constantes conocidos al compilar (`PIN_LED_ALERTA`). |
| `#include <WiFi.h>` / `"AdafruitIO_WiFi.h"` | Habilitan el stack WiFi del ESP32 y el cliente seguro TLS/MQTT de Adafruit IO. |

---

## 2. Funciones de Lectura ADC del ESP32 y Matemáticas

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `analogReadResolution(12)` | Define lectura ADC de 12 bits (`0..4095`). |
| `analogSetPinAttenuation(pinLM35, ADC_11db)` | Ajusta la escala de voltaje del pin ADC hasta ~3.1–3.3 V usando la constante `ADC_11db`. (Si solo midieras <950 mV podrías usar `ADC_0db` para aún más resolución, pero `ADC_11db` soporta rangos amplios sin saturar si el LM35 llega a >100°C). |
| `pinMode(..., OUTPUT)` / `digitalWrite(..., HIGH/LOW)` | Configura `PIN_LED_ALERTA` (`GPIO 26`) como salida digital y lo enciende (`HIGH`) o apaga (`LOW`) según la alerta térmica. |
| `analogReadMilliVolts(pinLM35)` | Devuelve la lectura en **milivoltios reales calibrados de fábrica** (`mV`). Para el LM35 (`10 mV/°C`), `mV / 10.0` da directamente `°C`. |
| `delayMicroseconds(200)` | Pausa de apenas `0.2 ms` (microsegundos, **no bloquea** WiFi ni MQTT) para estabilizar el capacitor interno del ADC entre muestras. |
| `constrain(x, min, max)` | Limita un valor dinámico dentro de un rango seguro (ej. `muestrasFiltro` entre `4` y `128`). |
| `max(a, b)` | Devuelve el mayor de dos números; usado para garantizar que `intervaloEnvioMs` nunca baje de `2200 ms` (protección anti-throttle). |
| `fabs(x)` | Valor absoluto para `float` (`float abs`): mide cuánto cambió la temperatura desde el último envío (*deadband*). |
| `strtol` / `strtof` (`parseIntSeguro` / `parseFloatSeguro`) | Convierte texto a entero/decimal verificando `endptr` para rechazar entradas no numéricas (`"abc"` o vacío) y evitar que un error de escritura reinicie el umbral a `0.0 °C`. |

---

## 3. Funciones de String y Control Dinámico por Serial

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `cmd.trim()` | Elimina espacios, tabuladores y saltos de línea al principio y final de un comando recibido. |
| `cmd.toUpperCase()` | Convierte toda la cadena a mayúsculas para aceptar `"umbral=30"` o `"UMBRAL=30"` indistintamente. |
| `cmd.startsWith("UMBRAL=")` | Devuelve `true` si el comando recibido empieza con ese prefijo. |
| `cmd.substring(7)` | Extrae la subcadena desde el índice `7` hasta el final (lo que viene después de `UMBRAL=`). |
| `.toFloat()` / `.toInt()` | Convierte una subcadena de texto a número decimal (`float`) o entero (`int`). |
| `.charAt(0)` | Obtiene el primer carácter de un `String` (por ejemplo `'C'`, `'F'` o `'K'`). |

---

## 4. Comandos y Callbacks de Adafruit IO

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `AdafruitIO_Feed *feedTemp` | Puntero al feed de telemetría `"corto3-temp"`. |
| `feedUmbral->onMessage(alRecibirUmbral)` | Conecta el feed `"corto3-umbral"` con nuestra función `alRecibirUmbral`. Cada vez que muevas el **Slider** en el Dashboard, Adafruit IO llama esa función automáticamente. |
| `AdafruitIO_Data *data` | Objeto que transporta el dato recibido desde el Dashboard. |
| `data->toFloat()` / `data->toString()` | Extrae el valor recibido del Dashboard como decimal o texto según el tipo de widget (`Slider` vs `Text Input`). |
| `feedUmbral->get()` | Solicita a Adafruit IO que reenvíe el último valor guardado en la nube al arrancar (o al reconectar con `aioSincronizado`), sincronizando tus variables dinámicas. |
| `io.status()` / `AIO_CONNECTED` / `io.statusText()` | Verifica si MQTT está conectado (`>= AIO_CONNECTED`) e imprime mensajes descriptivos de diagnóstico si falla WiFi o la llave. |
| `io.run(10, true)` | Mantiene viva la conexión MQTT y procesa mensajes entrantes del Dashboard con `fail_fast=true`. |
| `feedTemp->save(tempMostrar, 0, 0, 0, 2)` | Publica la temperatura al Dashboard. Los argumentos `(valor, lat, lon, ele, precision)` envían `2` decimales sin coordenadas GPS (`0, 0, 0`). `syncUmbralPendiente` respeta el intervalo mínimo si ajustas el umbral por Serial. |

---

## 5. Lógica de Histéresis y Anti-Throttle

- **Histéresis (`histeresisC`):** Si el umbral es `30.0°C` y la histéresis es `1.0°C`, la alerta se enciende al subir a `>= 30.0°C`, pero **no se apaga** hasta bajar a `<= 29.0°C`. Evita que un LED/relé/servo vibre rápido cuando la temperatura está en `29.99 / 30.01`.
- **Deadband + Latido (`cambioSignificativo || pasoLatido`):** Solo publica a Adafruit IO si la temperatura cambió `>= 0.25` grados o si pasaron `15 segundos` sin enviar nada, manteniendo el consumo muy por debajo del límite de `30 msg/min`.
