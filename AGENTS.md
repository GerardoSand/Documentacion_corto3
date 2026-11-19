# Reglas del Workspace — Documentacion_corto3 (ESP32 + Adafruit IO)

Este repositorio es el kit de supervivencia y resolución rápida para el **Corto 3 de Electrónica** (componentes: `LM35`, `Infrarrojo`, `Servomotor`, `RFID MFRC522` sobre `ESP32` conectados a Internet y `Adafruit IO`).

## Reglas Obligatorias de Hardware y Código

1. **Coordinación ICPC:** Para cualquier diseño, modificación, integración o depuración de circuitos/código, consulta o delega al agente especialista `icpc-electronica` (`.agents/agents/icpc-electronica.md`).
2. **Pines Seguros ESP32 (Cero Conflictos):**
   - **LM35 / IR Analógico:** Usar **exclusivamente ADC1** (`GPIO 32, 33, 34, 35, 36, 39`). **NUNCA** usar ADC2 (`0, 2, 4, 12, 13, 14, 15, 25, 26, 27`) cuando `WiFi` esté activo.
   - **Pines solo entrada (`34, 35, 36, 39`):** No tienen `INPUT_PULLUP` interno ni funcionan como salida PWM/SPI.
   - **RFID MFRC522:** Alimentar estrictamente a **3.3V** (5V lo destruye). VSPI: `SCK=18, MISO=19, MOSI=23, SS=5, RST=4` (no usar `GPIO 22` si se conecta OLED I2C en `SDA=21, SCL=22`).
   - **Servomotor:** Alimentación **5V externos / VIN** con **GND común** al ESP32. Nunca alimentar desde el pin `3V3`.
   - **Flash interna (`GPIO 6-11`):** Prohibido usar.
3. **Código No Bloqueante y Anti-Throttle:**
   - Prohibido usar `delay()` largos o bucles bloqueantes en `loop()`; siempre mantener `io.run()` ejecutándose fluidamente y temporizar con `millis()`.
   - Respetar el límite gratuito de **Adafruit IO**: máximo **30 publicaciones por minuto** (intervalo global `>= 2000 ms`) y máximo **10 feeds**.
4. **Valores Dinámicos y Explicación Completa:**
   - Todo sketch debe permitir ajustar parámetros dinámicamente (Adafruit IO Dashboard + Monitor Serial + persistencia `Preferences` cuando aplique).
   - Toda entrega de código nuevo debe explicar qué hace cada palabra reservada, librería, objeto, función y macro usada en ese código.
5. **Compilación Verificada:**
   - Antes de dar por bueno cualquier `.ino`, compilarlo con `arduino-cli compile --fqbn esp32:esp32:esp32` (si está disponible en la máquina).
   - Nunca subir credenciales reales (`secrets.h`) a Git.
