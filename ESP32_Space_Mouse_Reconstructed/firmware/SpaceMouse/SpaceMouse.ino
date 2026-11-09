#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <MPU6050_light.h>
#include <Adafruit_TinyUSB.h>
#include <math.h>

// Reconstructed from project notes, not a recovered original.
// Select USB Mode: TinyUSB in the ESP32-S3 board menu.

constexpr int SDA_PIN = 13;
constexpr int SCL_PIN = 12;
constexpr int MODE_BUTTON = 5;
constexpr int HOME_BUTTON = 6;

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* WS_HOST = "172.20.10.5"; // Replace with laptop IP on the same network
constexpr uint16_t WS_PORT = 3000;

constexpr float DEADZONE = 1.0f;
constexpr float TILT_RANGE = 35.0f;
constexpr float MAX_MOVE = 12.0f;
constexpr uint8_t REPORT_MOUSE = 1;
constexpr uint8_t REPORT_KEYBOARD = 2;

uint8_t const hid_descriptor[] = {
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_MOUSE)),
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_KEYBOARD))
};
Adafruit_USBD_HID usb_hid;
MPU6050 mpu(Wire);
WebSocketsClient ws;

struct Snapshot { float x; float y; bool orbit; };
Snapshot snapshot = {0, 0, true};
portMUX_TYPE snapshotLock = portMUX_INITIALIZER_UNLOCKED;
volatile bool recalibrateRequested = false;

void onWebSocket(WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_TEXT) {
    String cmd;
    for (size_t i = 0; i < len; ++i) cmd += char(payload[i]);
    cmd.trim();
    if (cmd.equalsIgnoreCase("RECALIBRATE")) recalibrateRequested = true;
  }
}

int8_t scaledMotion(float degrees) {
  float magnitude = fabsf(degrees);
  if (magnitude < DEADZONE) return 0;
  float scaled = fminf((magnitude - DEADZONE) / (TILT_RANGE - DEADZONE), 1.0f);
  int value = int(roundf(scaled * MAX_MOVE));
  if (value < 1) value = 1;
  return degrees < 0 ? -value : value;
}

void releaseHid() {
  if (!usb_hid.ready()) return;
  usb_hid.mouseReport(REPORT_MOUSE, 0, 0, 0, 0, 0);
  delay(2);
  if (usb_hid.ready()) usb_hid.keyboardRelease(REPORT_KEYBOARD);
}

void sendHome() {
  // Home is a standard HID key. Configure the CAD application if needed.
  releaseHid();
  if (usb_hid.ready()) {
    uint8_t keys[6] = {HID_KEY_HOME, 0, 0, 0, 0, 0};
    usb_hid.keyboardReport(REPORT_KEYBOARD, 0, keys);
    delay(8);
    if (usb_hid.ready()) usb_hid.keyboardRelease(REPORT_KEYBOARD);
  }
}

void motionTask(void* parameter) {
  bool orbit = true;
  bool lastModeHigh = true;
  bool lastHomeHigh = true;
  bool active = false;
  uint32_t lastModePress = 0;
  uint32_t lastHomePress = 0;
  float filteredX = 0, filteredY = 0;
  for (;;) {
    const uint32_t now = millis();
    bool modeHigh = digitalRead(MODE_BUTTON);
    bool homeHigh = digitalRead(HOME_BUTTON);
    if (lastModeHigh && !modeHigh && now - lastModePress > 200) {
      releaseHid(); active = false; orbit = !orbit;
      lastModePress = now;
    }
    if (lastHomeHigh && !homeHigh && now - lastHomePress > 200) {
      sendHome(); active = false;
      lastHomePress = now;
    }
    lastModeHigh = modeHigh;
    lastHomeHigh = homeHigh;

    if (recalibrateRequested) {
      recalibrateRequested = false;
      releaseHid(); active = false;
      // Keep the device motionless during calibration.
      mpu.calcOffsets(true, true);
      filteredX = filteredY = 0;
    }

    mpu.update();
    float measuredX = mpu.getAngleY(); // left/right
    float measuredY = mpu.getAngleX(); // forward/back
    filteredX = 0.8f * filteredX + 0.2f * measuredX;
    filteredY = 0.8f * filteredY + 0.2f * measuredY;
    int8_t dx = scaledMotion(filteredX);
    int8_t dy = scaledMotion(filteredY);
    portENTER_CRITICAL(&snapshotLock);
    snapshot.x = measuredX; snapshot.y = measuredY; snapshot.orbit = orbit;
    portEXIT_CRITICAL(&snapshotLock);

    bool moving = dx != 0 || dy != 0;
    if (!moving) {
      if (active) { releaseHid(); active = false; }
    } else if (usb_hid.ready()) {
      if (orbit) {
        // Shift + middle mouse button = Orbit in the original mapping.
        if (usb_hid.ready()) usb_hid.keyboardReport(REPORT_KEYBOARD, KEYBOARD_MODIFIER_LEFTSHIFT, nullptr);
        delay(2);
      }
      if (usb_hid.ready()) usb_hid.mouseReport(REPORT_MOUSE, MOUSE_BUTTON_MIDDLE, dx, dy, 0, 0);
      active = true;
    }
    vTaskDelay(pdMS_TO_TICKS(12));
  }
}

void networkTask(void* parameter) {
  ws.begin(WS_HOST, WS_PORT, "/");
  ws.onEvent(onWebSocket);
  ws.setReconnectInterval(2000);
  uint32_t lastMessage = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    ws.loop();
    if (millis() - lastMessage >= 100 && ws.isConnected()) {
      Snapshot current;
      portENTER_CRITICAL(&snapshotLock);
      current = snapshot;
      portEXIT_CRITICAL(&snapshotLock);
      char msg[96];
      snprintf(msg, sizeof(msg), "X:%.2f,Y:%.2f,Mode:%s", current.x, current.y, current.orbit ? "Orbit" : "Pan");
      ws.sendTXT(msg);
      lastMessage = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(MODE_BUTTON, INPUT_PULLUP);
  pinMode(HOME_BUTTON, INPUT_PULLUP);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (mpu.begin() != 0) {
    Serial.println("MPU6050 not detected; check I2C wiring.");
    while (true) delay(1000);
  }
  delay(500);
  mpu.calcOffsets(true, true);
  usb_hid.setReportDescriptor(hid_descriptor, sizeof(hid_descriptor));
  usb_hid.begin();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  xTaskCreatePinnedToCore(motionTask, "MotionHID", 8192, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(networkTask, "WebSocket", 8192, nullptr, 1, nullptr, 0);
}

void loop() { delay(1000); }
