# Explicación de Comandos y Palabras Reservadas — `02_infrarrojo_adafruit.ino`

Código funcional para módulos infrarrojos **FC-51 / TCRT5000 (obstáculo o seguidor de línea digital/analógico)** y **HC-SR501 (PIR)** con conteo de eventos, calibración dinámica y telemetría hacia **Adafruit IO**.

---

## 1. Tipos Enumerados y Estructuras de Estado

| Palabra / Comando | Qué hace en este código |
| :--- | :--- |
| `enum ModoIR { MODO_DO, MODO_AO, MODO_PIR };` | Define un tipo enumerado propio (`enum`) para representar de forma legible los 3 modos de operación posibles en runtime sin usar números mágicos. |
| `uint32_t &outMv` | El operador `&` en un parámetro significa **paso por referencia**: permite que `leerSensorIR(lecturaMv)` devuelva el booleano de detección **y al mismo tiempo** actualice `lecturaMv` en milivoltios sin crear copias. |
| `const char*` | Puntero a cadena constante en C (`"DO"`, `"AO"`, `"PIR"`), ligero en memoria para imprimir o concatenar. |

---

## 2. Funciones de Lectura, Calibración y Lógica Dinámica

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `digitalRead(PIN_IR_DO)` | Lee el estado lógico (`HIGH` o `LOW`) de la salida digital `DO` del módulo infrarrojo o `OUT` del PIR. |
| `analogReadMilliVolts(PIN_IR_AO)` | Lee el voltaje real en milivoltios de la salida analógica `AO` (útil cuando el potenciómetro no ajusta bien o te piden reaccionar según cercanía/reflectancia). |
| `map(val, 0, 100, 100, 3200)` | Función matemática de regla de tres lineal: convierte un porcentaje `0..100` del Slider web en milivoltios `100..3200 mV` dinámicamente. |
| `min(a, b)` | Devuelve el menor de dos valores; evita que al calibrar el umbral exceda el límite seguro de `3100 mV`. |
| `activoEnBajo ? (nivel == LOW) : (nivel == HIGH)` | Operador ternario (`cond ? si : no`): permite invertir dinámicamente en vivo si el sensor se activa con `0V` (`LOW`, común en FC-51) o con `3.3V` (`HIGH`). |
| `debounceMs` con `millis()` | Filtro **antirrebote temporal**: si un objeto vibra en el borde del haz IR, ignora rebotes ocurridos antes de `debounceMs` milisegundos, evitando conteos falsos. |
| `crudo != estadoAnterior` | Detecta **flanco de cambio**: el contador `conteoEventos++` solo suma 1 cuando pasa de `LIBRE` a `DETECTADO`, no mientras la mano siga frente al sensor. |
| `pinMode(..., INPUT/OUTPUT)` / `digitalWrite(..., HIGH/LOW)` | Configura `GPIO 32` como entrada digital limpia y `GPIO 26` como salida para el LED indicador. |
| `analogReadResolution(12)` / `analogSetPinAttenuation(..., ADC_11db)` | Prepara la lectura analógica `AO` de 12 bits hasta ~3.3V en `GPIO 35`. |
| `constrain(val, min, max)` / `delayMicroseconds(200)` | Limita `umbralAnalogMv` (`50..3300`) y `debounceMs` (`20..5000`); estabiliza el ADC durante `CALIBRAR` sin bloquear WiFi. |
| `strtol` / `strtof` / `roundf` | Validación estricta de números en `parseIntSeguro`/`parseFloatSeguro` para rechazar texto inválido y redondear porcentajes de Slider. |
| `ANALOG_POL=MENOR\|MAYOR` | Permite invertir dinámicamente la lógica analógica si tu módulo sube de voltaje al detectar en lugar de bajar. |
| `cmd.trim()` / `toUpperCase()` / `startsWith()` / `substring()` | Analizan comandos dinámicos por Serial o Dashboard (`MODO=AO`, `UMBRAL_MV=1500`, `RESET_IR`, etc.). |

---

## 3. Comunicación con Adafruit IO

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `feedIR->save(payload)` | Publica una cadena compacta (`"DETECTA|CNT:5|DO"`) en el feed `"corto3-ir"` para ver estado, conteo y modo activo de un vistazo en un bloque **Text** o **Stream**. |
| `feedCmd->onMessage(alRecibirComando)` | Permite recibir palabras de orden como `"RESET_IR"`, `"CALIBRAR"`, `"ACTIVO=LOW"`, `"DEBOUNCE=250"` o `"UMBRAL_MV=1500"` desde el Dashboard. |
| `feedModo->get()` / `feedUmbral->get()` | Recuperan el último modo y umbral del Dashboard al conectar (con reintento automático si WiFi tardó en `setup()`). |
| `io.run(10, true)` / `AIO_CONNECTED` | Procesa MQTT con `fail_fast=true` y verifica que la conexión siga activa antes de publicar. |
| `hayPendienteEnvio` | Bandera booleana que marca un cambio real de estado para enviarlo apenas se cumpla la ventana mínima de `2200 ms` (nunca bloquea el `loop()` ni provoca throttle). |
