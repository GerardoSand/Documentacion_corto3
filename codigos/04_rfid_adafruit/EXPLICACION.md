# Explicación de Comandos y Palabras Reservadas — `04_rfid_adafruit.ino`

Código funcional para **RFID MFRC522** conectado por SPI al ESP32, con base de datos dinámica de tarjetas guardada en Flash (**no se borra al reiniciar**), lectura/escritura de bloques internos de tarjetas MIFARE Classic y registro en **Adafruit IO**.

---

## 1. Contenedores C++ y Tipos de Datos

| Palabra / Comando | Qué hace en este código |
| :--- | :--- |
| `#include <vector>` | Importa el contenedor dinámico `std::vector` de C++ estándar para mantener una lista de UIDs de tamaño variable en RAM sin límite fijo rígido. |
| `std::vector<String> tarjetasAutorizadas;` | Lista dinámica en tiempo de ejecución donde se agregan (`push_back`) o borran (`erase`) UIDs recibidos desde Adafruit IO, Serial o modo `LEARN`. |
| `size_t` | Tipo entero sin signo usado para índices y tamaños de arreglos/vectores (`tarjetasAutorizadas.size()`). |
| `const String &uid` | Paso por referencia constante: evita duplicar memoria del objeto `String` al buscar un UID. |
| `memset(buffer, ' ', sizeof(buffer))` | Llena un arreglo de `16` bytes con espacios antes de escribir en la tarjeta MIFARE para no dejar basura de memoria anterior. |

---

## 2. Comandos y Estructuras de `MFRC522`

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST)` | Crea el controlador del módulo RFID asignando Chip Select (`5`) y Reset (`4`). |
| `SPI.begin(SCK, MISO, MOSI, SS)` / `rfid.PCD_Init()` | Arranca el bus VSPI (`18,19,23,5`) e inicializa el lector MFRC522 a 13.56 MHz. |
| `rfid.PCD_ReadRegister(MFRC522::VersionReg)` | Verifica comunicación SPI leyendo la versión del chip (`0x91`/`0x92` original, `0x88` clon; `0x00`/`0xFF` indica fallo de cableado o 3.3V). |
| `rfid.PICC_IsNewCardPresent()` / `PICC_ReadCardSerial()` | Detecta si entró una tarjeta en el campo RF y lee su UID en `rfid.uid`. |
| `MFRC522::MIFARE_Key llaveFabrica` | Estructura de 6 bytes (`0xFF` × 6) que es la clave A estándar de fábrica de las tarjetas/llaveros MIFARE Classic 1K. |
| `rfid.uid` (`rfid.uid.size`, `rfid.uid.uidByte`) | Estructura rellenada tras leer una tarjeta: `size` indica si el UID tiene `4`, `7` o `10` bytes, y `uidByte[]` contiene esos bytes. |
| `rfid.PCD_Authenticate(PICC_CMD_MF_AUTH_KEY_A, bloque, &llaveFabrica, &(rfid.uid))` | Autentica el acceso criptográfico a un sector de memoria MIFARE antes de leer o escribir un bloque de datos. |
| `rfid.MIFARE_Read(bloque, buffer, &tam)` | Lee `16` bytes de datos (+ `2` bytes CRC = `18`) desde el bloque indicado de la tarjeta. |
| `rfid.MIFARE_Write(bloque, buffer, 16)` | Graba `16` bytes en un bloque de datos de la tarjeta (por ejemplo nombre, saldo o contador). **Regla de seguridad:** el código bloquea el bloque `0` y los bloques trailer `(bloque % 4 == 3)` para no dañar la tarjeta permanentemente. |
| `rfid.PICC_HaltA()` | Pone la tarjeta actual en modo `HALT` para que no responda repetidamente en el mismo instante. |
| `rfid.PCD_StopCrypto1()` | Cierra la encriptación activa en el lector; **si se omite cuando lees/escribes bloques, el lector no volverá a leer ninguna otra tarjeta hasta reiniciar.** |

---

## 3. Persistencia NVS y Comandos Dinámicos

| Palabra / Comando | Qué hace |
| :--- | :--- |
| `Preferences prefs;` | Guarda la lista de UIDs serializada (`"AA:BB:CC:DD,11:22:33:44"`) en la partición NVS de la Flash bajo el espacio `"rfid_db"`. |
| `tarjetasAutorizadas.push_back(uid)` | Añade un nuevo UID al final de la lista dinámica en RAM. |
| `tarjetasAutorizadas.erase(tarjetasAutorizadas.begin() + idx)` | Elimina un UID específico de cualquier posición de la lista. |
| `isxdigit()` / `charAt()` / `indexOf()` / `substring()` / `min()` | Normalizan UIDs (aceptan tanto `"AA:BB:CC:DD"` como `"AABBCCDD"`), parsean la lista guardada en NVS y limitan la escritura de bloque a máximo `16` bytes. |
| `digitalWrite()` / `digitalRead()` | Encienden el LED de acceso (`GPIO 26`) al detectar tarjeta y lo apagan sin `delay()` tras `1200 ms`. |
| `CANCEL` | Cancela un modo pendiente (`LEARN`, `READ_BLOQUE` o `WRITE_BLOQUE=`) si cambiaste de opinión antes de acercar una tarjeta. |
| `cooldownRFIDMs` con `millis()` | Evita múltiples lecturas seguidas de la misma tarjeta y protege la cuota de `30 req/min` de Adafruit IO. |
