# ESP32 Space Mouse — project files


## Contents

- `firmware/SpaceMouse/SpaceMouse.ino` — ESP32-S3 Zero Arduino sketch: MPU6050, TinyUSB USB HID mouse/keyboard, Orbit/Pan mode, Home button, FreeRTOS motion and networking tasks, and Wi-Fi telemetry.
- `server/server.js` — Node.js Express and WebSocket server, default port 3000.
- `server/public/index.html` — real-time browser dashboard and recalibrate button.
- `server/package.json` — Node.js dependencies.

## Hardware and setup

- ESP32-S3 Zero / ESP32-S3 board
- MPU6050 over I2C: SDA GPIO 13, SCL GPIO 12, plus power and ground appropriate for the module
- Mode button GPIO 5 to ground; Home button GPIO 6 to ground (internal pull-ups)
- USB data cable for HID connection to host computer

## Arduino setup

1. Install an ESP32 board package with ESP32-S3 and TinyUSB USB-mode support.
2. Install `MPU6050_light`, `WebSockets` (WebSocketsClient), and `Adafruit TinyUSB Library`. `WiFi` and `Wire` come from the ESP32 core.
3. Select an ESP32-S3 board and select **TinyUSB** USB mode where available.
4. Edit `WIFI_SSID`, `WIFI_PASSWORD` and `WS_HOST` near the top of `SpaceMouse.ino`. `WS_HOST` must be your computer's reachable IP address.
5. Put the device on a stable surface during boot/calibration, then flash.
6. Test HID input in a harmless environment before using it in Fusion 360. CAD orbit mappings can differ by application/configuration.

## Web dashboard

```sh
cd server
npm install
npm start
```

Open `http://localhost:3000` on the host computer. The ESP32 and computer must share a reachable network. Browser sends `RECALIBRATE` to connected devices; the board does calibration in its motion task.

## Design details / known differences

- Orbit = Shift + middle-button drag; Pan = middle-button drag. The Home button emits the HID Home key; adapt the mapping if Fusion 360 requires another command.
- Movement scale is capped at ±12, dead zone 1 degree, tilt range 35 degrees. Tune these for your device.

