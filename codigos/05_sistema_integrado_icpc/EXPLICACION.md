# Explicación de Comandos y Palabras Reservadas — `05_sistema_integrado_icpc.ino`

Este es el **código maestro completo del Corto 3**: coordina los 4 módulos (`LM35 + Infrarrojo + Servomotor + RFID MFRC522`) con valores dinámicos en tiempo real desde **Adafruit IO Dashboard** y **Monitor Serial**, sin bloqueos y con protección anti-throttle.

---

## 1. Arquitectura de Tiempos y Anti-Throttle

| Palabra / Variable | Qué hace en este código |
| :--- | :--- |
| `dirtyRFID`, `dirtyServo`, `dirtyTemp`, `dirtyIR` | Banderas booleanas ("datos sucios") que indican qué sensor tiene un dato nuevo pendiente por subir al Dashboard. |
| `INTERVALO_GLOBAL_AIO_MS = 2300` | Garantiza como máximo `60000 / 2300 ≈ 26` envíos por minuto en total, quedando seguro bajo el límite gratuito de **30 datos/min** de Adafruit IO. |
| `turnoRoundRobin` | Variable de turno rotativo (`0` o `1`) para que `LM35` e `IR` se alternen el derecho a publicar sin que uno acapare al otro. |
| `return;` temprano en `despacharAdafruitIO` | Cuando un feed con mayor prioridad (`RFID` > `Servo` > `Temp/IR`) publica en este turno, sale inmediatamente de la función para esperar otros `2300 ms` antes del siguiente `save()`. |

---

## 2. Lógica Combinada de Control (Cómo Adaptarla en el Examen)

| Función | Qué coordina exactamente |
| :--- | :--- |
| `actualizarLM35(ahora)` | Mide milivoltios calibrados cada `500 ms` en `GPIO 34`, calcula `tempActualC` y evalúa la alerta térmica contra `umbralTempC` con histéresis dinámica. |
| `actualizarIR(ahora)` | Lee `GPIO 32` con antirrebote de `200 ms`, detecta flanco de objeto, suma `conteoIR++` y activa `irDetectado`. |
| `actualizarRFID(ahora)` | Cada `2000 ms` revisa si hay tarjeta; si `modoLearnRFID` está activo la guarda en NVS Flash; si no, valida contra `listaUIDs` y activa `compuertaAbiertaPorRFID = true`. |
| `calcularObjetivoServo(ahora)` | Decide el ángulo destino del servo según las reglas dinámicas activas: en `MANUAL` obedece el Slider de Adafruit IO; en `AUTO` abre si hay acceso RFID o sobretemperatura (`tempAbreServo`), y **bloquea el cierre si `seguridadIR` detecta obstáculo**. |
| `moverServoSuave(ahora)` | Avanza `1` grado cada `msPorGrado` milisegundos sin usar `delay()`, permitiendo que RFID, IR, LM35 y WiFi sigan corriendo en paralelo. |

---

## 3. Palabras Reservadas, Librerías y Métodos Clave Usados

| Palabra / Comando | Qué hace en este código |
| :--- | :--- |
| `constexpr` / `const` / `uint8_t` / `unsigned long` / `float` / `bool` | Tipos y calificadores C++ para pines, tiempos `millis()`, temperaturas decimales y banderas de estado. |
| `std::vector<String>` | Lista dinámica de UIDs autorizados cargados desde `Preferences` (`begin`, `getString`, `putString`, `end`) en el espacio `"corto3_rfid"`. |
| `analogReadResolution(12)` / `analogSetPinAttenuation(..., ADC_11db)` / `analogReadMilliVolts()` | Lectura calibrada de fábrica del LM35 en `mV` sobre `ADC1` (`pinLM35 = 34` por defecto, reasignable con `PIN_LM35=`). |
| `delayMicroseconds(150)` | Pausa de apenas `0.15 ms` entre muestras ADC para estabilizar el convertidor sin bloquear WiFi/MQTT. |
| `ESP32PWM::allocateTimer(0)` / `setPeriodHertz(50)` / `attach()` / `detach()` / `write()` | Configuran y mueven el servo a 50 Hz; `detach()`/`attach()` permiten reasignar pin en vivo con `PIN_SERVO=13`. |
| `SPI.begin(18, 19, 23, 5)` / `rfid.PCD_Init()` / `PCD_ReadRegister()` | Inicializa el bus VSPI y verifica el chip MFRC522 (`VersionReg`). |
| `rfid.PICC_HaltA()` + `rfid.PCD_StopCrypto1()` | Cierre obligatorio de cada lectura RFID para evitar bloqueos del lector SPI. |
| `io.run(10, true)` / `feed->onMessage(...)` / `feed->get()` / `feed->save()` | Mantiene viva la sesión MQTT sin congelar sensores si cae la red, suscribe los 4 feeds de control y sincroniza al conectar (`aioSincronizado`). |
| `constrain()` / `fabs()` / `abs()` | Limitan rangos de ángulos/tiempos, miden variación térmica (`0.5 °C`) y protegen contra cierre en `seguridadIR` incluso si `ANG_CLOSE > ANG_OPEN`. |
| `strtol()` / `strtof()` / `roundf()` / `isxdigit()` | Validan comandos numéricos (`parseIntSeguro`/`parseFloatSeguro`) y normalizan UIDs (`AA:BB:CC:DD` o `AABBCCDD`). |
| `VEL=` / `PIN_SERVO=` / `PIN_LM35=` / `IR_ACTIVO=` | Comandos dinámicos extra para ajustar velocidad del servo, cambiar pines seguros o invertir polaridad del IR sin recompilar. |
