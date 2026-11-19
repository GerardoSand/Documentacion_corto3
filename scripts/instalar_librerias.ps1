# Script de instalación de librerías requeridas para Corto 3 (ESP32 + Adafruit IO)
# Ejecutar en PowerShell si cambias de computadora en el laboratorio.

$arduinoCli = Get-Command arduino-cli -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $arduinoCli) {
    $fallback = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
    if (Test-Path $fallback) {
        $arduinoCli = $fallback
    } else {
        Write-Error "No se encontró arduino-cli. Instala las librerías desde Arduino IDE -> Gestor de Bibliotecas."
        exit 1
    }
}

$librerias = @(
    "Adafruit IO Arduino",
    "Adafruit MQTT Library",
    "ArduinoHttpClient",
    "ArduinoJson",
    "ESP32Servo",
    "MFRC522",
    "IRremote",
    "WiFiManager",
    "Adafruit SH110X",
    "Adafruit GFX Library",
    "PubSubClient"
)

Write-Host "Instalando librerías necesarias para Corto 3 usando: $arduinoCli" -ForegroundColor Cyan
foreach ($lib in $librerias) {
    Write-Host " -> Instalando $lib..." -ForegroundColor Yellow
    & $arduinoCli lib install "$lib"
}

Write-Host "`nListo. Verifica también que tengas instalado el core 'esp32' by Espressif Systems." -ForegroundColor Green
