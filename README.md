# Documentacion_corto3 — Kit ICPC de Electrónica (ESP32 + Adafruit IO)

Componentes del corto: **LM35, Infrarrojo, Servomotor, RFID (`MFRC522`)** conectados a Internet (**WiFi + Adafruit IO Dashboard**) con **valores dinámicos**.

> **Uso en laboratorio (sin acceso externo):** Todo en esta carpeta está diseñado para abrirse directo en **Arduino IDE** (compilación verificada para **ESP32**), con pinout único sin conflictos, tablero único en Adafruit IO y explicación práctica de cada palabra reservada y comando usado en los códigos.

---

## 1. Agente Coordinador Tipo ICPC (`icpc-electronica`)

Se configuró el agente experto **`.agents/agents/icpc-electronica.md`** junto con las reglas en **`AGENTS.md`** y la skill **`.agents/skills/icpc-corto3-lab/SKILL.md`**.

- **En Antigravity CLI:** Ejecuta `agy --agent icpc-electronica` dentro de esta carpeta para resolver o adaptar cualquier enunciado del corto con auditoría estricta de pines, tiempos y compilación.
- **Qué coordina:** Evita conflictos `ADC2 + WiFi`, protege contra sobrevoltajes (`RFID 3.3V`, `Servo 5V externos + GND común`), bloquea `delay()` que rompen MQTT y previene *throttling* en Adafruit IO.

---

## 2. Ruta Rápida el Día del Corto (Orden de Uso)

1. **Checklist de hardware y pines seguros (leer primero):**
   [docs/00_icpc_hardware_y_pinout.md](docs/00_icpc_hardware_y_pinout.md)
2. **Lógica interna de Adafruit IO y creación del Dashboard único (8 feeds):**
   [docs/01_logica_adafruit_io.md](docs/01_logica_adafruit_io.md)
3. **Configuración de credenciales:**
   Usa [config/secrets_template.h](config/secrets_template.h) (opcionalmente cópialo como `secrets.h` dentro del sketch que subas, o edita directo las constantes al inicio del `.ino`).
4. **Si cambias de PC en el laboratorio (instalar librerías faltantes):**
   Ejecuta [scripts/instalar_librerias.ps1](scripts/instalar_librerias.ps1).

---

## 3. Códigos Funcionales + Explicación de Comandos y Palabras Reservadas

Todos los sketches comparten **el mismo mapa de pines** y **los mismos 8 feeds de Adafruit IO**, por lo que puedes probar por partes y luego subir el sistema integrado **sin recablear nada ni rehacer el Dashboard**.

| Carpeta | Propósito | Código Funcional | Explicación Palabra por Palabra |
| :--- | :--- | :--- | :--- |
| **00. Diagnóstico** | Verifica en 10 s hardware, pines, chip RFID, WiFi y Adafruit IO | [00_diagnostico_hardware.ino](codigos/00_diagnostico_hardware/00_diagnostico_hardware.ino) | [EXPLICACION.md](codigos/00_diagnostico_hardware/EXPLICACION.md) |
| **01. LM35** | Temperatura calibrada (`mV`), filtro dinámico, unidad `C/F/K`, umbral/histéresis en vivo | [01_lm35_adafruit.ino](codigos/01_lm35_adafruit/01_lm35_adafruit.ino) | [EXPLICACION.md](codigos/01_lm35_adafruit/EXPLICACION.md) |
| **02. Infrarrojo (DO/AO/PIR)** | Obstáculo/línea digital o analógico + PIR, calibración y conteo con antirrebote | [02_infrarrojo_adafruit.ino](codigos/02_infrarrojo_adafruit/02_infrarrojo_adafruit.ino) | [EXPLICACION.md](codigos/02_infrarrojo_adafruit/EXPLICACION.md) |
| **02b. IR Control Remoto** | Decodificador NEC/Sony/LG + modo aprendizaje dinámico en Flash NVS | [02b_ir_control_remoto_adafruit.ino](codigos/02b_ir_control_remoto_adafruit/02b_ir_control_remoto_adafruit.ino) | [EXPLICACION.md](codigos/02b_ir_control_remoto_adafruit/EXPLICACION.md) |
| **03. Servomotor** | `ESP32Servo` suave con `millis()`, límites/pulsos y velocidad dinámicos desde Slider | [03_servomotor_adafruit.ino](codigos/03_servomotor_adafruit/03_servomotor_adafruit.ino) | [EXPLICACION.md](codigos/03_servomotor_adafruit/EXPLICACION.md) |
| **04. RFID MFRC522** | Base de UIDs dinámica persistente (NVS Flash), `LEARN`, `ADD`/`DEL` y bloques MIFARE | [04_rfid_adafruit.ino](codigos/04_rfid_adafruit/04_rfid_adafruit.ino) | [EXPLICACION.md](codigos/04_rfid_adafruit/EXPLICACION.md) |
| **05. Integrado ICPC** | **Código Maestro:** los 4 módulos juntos + máquina de estados + cola anti-throttle | [05_sistema_integrado_icpc.ino](codigos/05_sistema_integrado_icpc/05_sistema_integrado_icpc.ino) | [EXPLICACION.md](codigos/05_sistema_integrado_icpc/EXPLICACION.md) |
| **06. Plan B Lab** | `WiFiManager` (portal cautivo celular) + pantalla OLED SH1106 + `Adafruit_MQTT`/`JSON` | [06_respaldo_wifimanager_oled.ino](codigos/06_respaldo_wifimanager_oled/06_respaldo_wifimanager_oled.ino) | [EXPLICACION.md](codigos/06_respaldo_wifimanager_oled/EXPLICACION.md) |

---

## 4. Resumen de Pines Seguros (Referencia Ultra-Rápida)

- **LM35 `OUT`** -> `GPIO 34` *(VCC del LM35 a `VIN / 5V`)*
- **IR Obstáculo `DO`** -> `GPIO 32` *(y `AO` opcional -> `GPIO 35`; VCC a `3.3V`)*
- **IR Control Remoto `OUT`** -> `GPIO 27` *(VCC a `3.3V`)*
- **Servomotor Señal** -> `GPIO 25` *(VCC a **`5V` externos + `GND` común**, NUNCA a `3.3V`)*
- **RFID MFRC522 (`3.3V ESTRICTO`)** -> `SS/SDA=5`, `SCK=18`, `MISO=19`, `MOSI=23`, `RST=4`
- **OLED SH1106 I2C (opcional)** -> `SDA=21`, `SCL=22`
- **LED indicador (opcional)** -> `GPIO 26`
