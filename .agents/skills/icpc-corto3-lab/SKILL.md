---
name: icpc-corto3-lab
description: >-
  Guía operativa tipo ICPC para resolver ejercicios del Corto 3 de electrónica con
  ESP32, LM35, Infrarrojo, Servomotor, RFID MFRC522 y Adafruit IO. Usar cuando se
  pida adaptar, combinar o depurar códigos para el laboratorio.
---

# Protocolo Rápido ICPC — Corto 3

Cuando el usuario presente un enunciado o problema nuevo durante el Corto 3, sigue estos 5 pasos estrictos:

1. **Descomponer el Enunciado (Entradas / Lógica / Salidas / Dinámicos):**
   - Identifica qué sensores intervienen (`LM35`, `IR`, `RFID`), qué actuadores (`Servomotor`, LED/OLED), qué feeds de `Adafruit IO` entran/salen y qué valores deben ser dinámicos en vivo.
2. **Validar Pinout y Alimentación:**
   - Toma como base la tabla segura en [docs/00_icpc_hardware_y_pinout.md](../../../docs/00_icpc_hardware_y_pinout.md) para evitar choques ADC2+WiFi, pines strapping o sobrevoltajes.
3. **Partir de la Plantilla Funcional Más Cercana:**
   - Individuales: `codigos/01_lm35_adafruit/` a `codigos/04_rfid_adafruit/` (y `02b_ir_control_remoto_adafruit/` para control NEC).
   - Combinado: `codigos/05_sistema_integrado_icpc/`.
   - Emergencia sin WiFi fijo / con OLED: `codigos/06_respaldo_wifimanager_oled/`.
4. **Inyectar Lógica Dinámica + Anti-Throttle:**
   - Usa `millis()` para todo temporizador, mantén `io.run()` al inicio de `loop()` y usa el scheduler de publicación (`>= 2200 ms` entre envíos globales).
5. **Auditar y Compilar:**
   - Invoca o aplica las reglas de `icpc-electronica` y compila con:
     `arduino-cli compile --fqbn esp32:esp32:esp32 <ruta_al_sketch>`
   - Entrega el código compilado más la tabla explicativa de las palabras reservadas/comandos nuevos usados.
