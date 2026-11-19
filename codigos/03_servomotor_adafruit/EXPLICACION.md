# Explicación de Comandos y Palabras Reservadas — `03_servomotor_adafruit.ino`

Código funcional para controlar un **servomotor en ESP32** con velocidad suave sin bloqueos (`millis()`), calibración dinámica de límites/pulsos y control bidireccional desde **Adafruit IO** y **Monitor Serial**.

---

## 1. Comandos de la Librería `ESP32Servo`

| Palabra / Comando | Qué hace en este código |
| :--- | :--- |
| `#include <ESP32Servo.h>` | Importa la librería específica de servos para ESP32 (no usar `Servo.h` de Arduino AVR porque falla al compilar en ESP32). |
| `Servo miServo;` | Crea la instancia de control del servomotor. |
| `ESP32PWM::allocateTimer(0)` | Asigna uno de los timers hardware PWM del ESP32 para que la señal de 50 Hz no choque con otras salidas. |
| `miServo.setPeriodHertz(50)` | Fija la frecuencia estándar de `50 Hz` (`20 ms` de periodo). |
| `miServo.attach(pinServo, minUs, maxUs)` | Conecta el objeto al pin GPIO (`25` por defecto o el elegido con `PIN=`) y define los pulsos mínimo (`500 µs` = `0°`) y máximo (`2400 µs` = `180°`). `ESP32Servo` exige límites internos `500..2500 µs`. |
| `miServo.detach()` | Desconecta temporalmente el PWM del pin para permitir recalibrar los pulsos o cambiar de pin (`PIN=13`) dinámicamente con `attach()` sin errores. |
| `miServo.write(anguloActual)` | Envía el ángulo deseado (`0` a `180`). Internamente, si el valor es `< 500`, `ESP32Servo` lo interpreta como grados; si es `>= 500`, lo trata como microsegundos. |

---

## 2. Manejo Dinámico, Parsing y Control No Bloqueante

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `bool` / `int` / `uint8_t` / `unsigned long` | Tipos de estado: bandera lógica (`modoBarrido`), ángulos enteros (`0..180`), pin dinámico (`pinServo`) y temporizadores de `millis()`. |
| `constrain(nuevo, anguloMinimo, anguloMaximo)` | Impide que un ángulo dinámico recibido por red o Serial exceda los topes mecánicos configurados (`LIMITES=10,170`). |
| `cmd.indexOf(',')` / `startsWith()` / `substring()` / `trim()` / `toUpperCase()` | Limpian y separan comandos compuestos como `"LIMITES=10,170"`, `"PULSO=500,2400"` o `"PIN=13"`. |
| `strtol` / `strtof` / `roundf` | Validan entradas numéricas en `parseIntSeguro`/`parseFloatSeguro` para que texto inválido (`"abc"`) no mueva el servo accidentalmente a `0°`. |
| `esPinServoSeguro(gpio)` | Verifica antes de aplicar `PIN=` que el GPIO elegido soporte PWM y no choque con Flash (`6-11`), UART (`1,3`), strapping (`0,2,12,15`) ni pines solo-entrada (`34-39`). |
| `if (ahora - ultimoPasoServoMs >= msPorGrado)` | Reemplaza al típico `for (...) { delay(15); }`. Mueve **1 grado cada `msPorGrado` milisegundos**, permitiendo que WiFi, MQTT (`io.run()`) y otros sensores sigan respondiendo a máxima velocidad mientras el servo gira. |
| `onMessage()` / `get()` / `io.run(10, true)` / `save()` / `AIO_CONNECTED` | Suscriben el Slider (`corto3-servo-cmd`) y el modo (`corto3-modo`), recuperan el estado inicial al conectar, procesan paquetes sin bloquear y publican el ángulo final alcanzado. |
| `!modoBarrido` al reportar | En modo barrido continuo, evita inundar Adafruit IO con cada ángulo intermedio y evita ser bloqueado por *throttle* (`30 req/min`). |
