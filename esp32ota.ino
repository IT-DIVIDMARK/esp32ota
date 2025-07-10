#include <SPI.h>
#include <TFT_eSPI.h> 
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>
#include "DHT.h"

// WiFi
const char* ssid = "admin";
const char* password = "password";

// OTA Version check
const char* versionURL = "https://raw.githubusercontent.com/IT-DIVIDMARK/esp32ota/main/version.json";
String firmwareURL;
#define FIRMWARE_VERSION "1.0.18"

// DHT22 Sensor
#define DHTPIN 25
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
float temperature = 0;
float humidity = 0;

// Devices
#define MAX_DEVICES 10
struct Device {
  String name;
  int gpio;
  bool state;
};
Device devices[MAX_DEVICES];
int deviceCount = 0;

Preferences preferences;
WebServer server(80);


TFT_eSPI tft = TFT_eSPI();  // Create TFT object

// Define LED pins
//const int ledPins[4] = {26, 27, 32, 33};

// Touch button frame settings
#define FRAME_X 40
#define FRAME_Y 50
#define FRAME_W 240
#define FRAME_H 180
#define BUTTON_W (FRAME_W / 2)
#define BUTTON_H (FRAME_H / 2)

// Touch calibration data from TFT_eSPI "Touch_calibrate" example
uint16_t calData[5] = {275, 3590, 285, 3541, 7};  // Replace with your values

//bool ledState[4] = {false, false, false, false};


// Function Prototypes
void handleRoot();
void handleAddDevice();
void handleRemoveDevice();
void handleNotFound();
void handleSensorData();
void loadDevices();
void saveDevices();
void updateDeviceState(int gpio, bool state);
//void displayStatus(String status);
void startWebServer();
void downloadFirmware(String url);
bool checkForUpdate();
void drawButtons();
void updateButton(int index);
int getButton(int x, int y);
void drawButtons();
void drawWiFiStatus();
void drawDeviceButton(int index);

// OTA functions (same as before)
void downloadFirmware(String url) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    if (contentLength <= 0) return;
    bool canBegin = Update.begin(contentLength);
    if (canBegin) {
      WiFiClient& client = http.getStream();
      size_t written = Update.writeStream(client);
      if (written == contentLength && Update.end()) {
       // displayStatus("Updated! Rebooting...");
        delay(2000);
        ESP.restart();
      }
    }
  }
  http.end();
}

bool checkForUpdate() {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(versionURL);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) return false;
    String latestVersion = doc["version"];
    firmwareURL = doc["firmware"].as<String>();
    if (latestVersion != FIRMWARE_VERSION) return true;
  }
  http.end();
  return false;
}

// Save/Load Devices
void saveDevices() {
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.createNestedArray("devices");
  for (int i = 0; i < deviceCount; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["name"] = devices[i].name;
    obj["gpio"] = devices[i].gpio;
    obj["state"] = devices[i].state;
  }
  String jsonString;
  serializeJson(doc, jsonString);
  preferences.begin("devices", false);
  preferences.putString("config", jsonString);
  preferences.end();
}

void loadDevices() {
  preferences.begin("devices", true);
  String jsonString = preferences.getString("config", "");
  preferences.end();
  if (jsonString.length() == 0) return;
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) return;
  JsonArray arr = doc["devices"].as<JsonArray>();
  deviceCount = 0;
  for (JsonObject obj : arr) {
    if (deviceCount >= MAX_DEVICES) break;
    devices[deviceCount].name = obj["name"].as<String>();
    devices[deviceCount].gpio = obj["gpio"].as<int>();
    devices[deviceCount].state = obj["state"].as<bool>();
    pinMode(devices[deviceCount].gpio, OUTPUT);
    digitalWrite(devices[deviceCount].gpio, devices[deviceCount].state ? HIGH : LOW);
    deviceCount++;
  }
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no" />
  <title>Smart Switch Pro</title>
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.0/css/all.min.css" />
  <style>
   :root {
      --primary: #3b82f6;
      --bg: #f9fafb;
      --text: #111827;
      --card: #fff;
      --muted: #6b7280;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Segoe UI', Arial, sans-serif; }
    body { min-height: 100vh; display: flex; flex-direction: column; background: var(--bg); color: var(--text); }
    header { display: flex; justify-content: space-between; align-items: center; padding: 14px 20px; background: var(--primary); color: #fff; font-size: 1.4rem; font-weight: 600; box-shadow: 0 2px 6px rgba(0,0,0,.1); }
    .bell-wrapper { position: relative }
    .bell-wrapper i { font-size: 1.3rem; cursor: pointer }
    .badge { position: absolute; top: -4px; right: -8px; height: 16px; min-width: 16px; padding: 0 4px; font-size: .65rem; line-height: 16px; background: #e11d48; color: #fff; border-radius: 999px; font-weight: 600; text-align: center; }
    main { flex: 1; padding: 16px; overflow-y: auto }
    .section { display: none }
    .section.active { display: block }
    .card { background: var(--card); border-radius: 12px; padding: 16px; margin-bottom: 16px; box-shadow: 0 1px 4px rgba(0,0,0,.06) }
    .card-title { font-weight: 600; margin-bottom: 8px }
    .device-card-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 12px }
    .device-card { position: relative; background: var(--card); border-radius: 20px; padding: 20px 16px; display: flex; flex-direction: column; align-items: center; gap: 18px; box-shadow: 0 4px 12px rgba(0,0,0,.06) }
    .device-icon i { font-size: 3rem; color: var(--primary) }
    .device-name { font-size: 1rem; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis }
    .switch { position: relative; width: 46px; height: 26px }
    .switch input { opacity: 0; width: 0; height: 0 }
    .slider { position: absolute; top: 0; left: 0; right: 0; bottom: 0; background: #d1d5db; border-radius: 15px; transition: .2s }
    .slider:before { content: ''; position: absolute; height: 22px; width: 22px; left: 2px; top: 2px; background: #fff; border-radius: 50%; transition: .2s }
    .switch input:checked + .slider { background: var(--primary) }
    .switch input:checked + .slider:before { transform: translateX(20px) }
    .remove-btn { position: absolute; top: 10px; right: 10px; border: none; background: #f3f4f6; color: var(--muted); padding: 6px; border-radius: 50%; cursor: pointer }
    .btn { display: inline-flex; align-items: center; gap: 8px; padding: 10px 16px; border: none; border-radius: 8px; background: var(--primary); color: #fff; font-size: .9rem; font-weight: 500; cursor: pointer }
    .fab { position: fixed; right: 20px; bottom: 80px; width: 56px; height: 56px; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-size: 1.5rem; background: var(--primary); color: #fff; box-shadow: 0 3px 8px rgba(0,0,0,.3) }
    nav { height: 60px; display: flex; background: var(--card); border-top: 1px solid #e5e7eb }
    nav button { flex: 1; border: none; background: none; font-size: .8rem; color: var(--muted); display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px }
    nav button.active { color: var(--primary) }
    .modal { position: fixed; inset: 0; display: none; align-items: center; justify-content: center; background: rgba(0,0,0,.45); z-index: 40 }
    .modal.open { display: flex }
    .modal-content { background: var(--card); width: 90%; max-width: 380px; border-radius: 16px; padding: 20px; box-shadow: 0 4px 16px rgba(0,0,0,.25); animation: pop .25s ease }
    @keyframes pop { 0% { transform: scale(.9); opacity: 0 } 100% { transform: scale(1); opacity: 1 } }
    .empty-state { display: flex; flex-direction: column; align-items: center; gap: 6px; padding: 40px 20px; color: var(--muted); font-size: .9rem }
    .empty-state i { font-size: 3rem; opacity: .4 }
  </style>
</head>
<body>

<header>
  <span>Smart Switch Pro</span>
  <div class="bell-wrapper">
    <i class="fa-solid fa-bell"></i><span class="badge">1</span>
  </div>
</header>

<main>
  <section id="dashboard" class="section active">
    <div class="card">
      <div class="card-title">My Appliances</div>
      <div id="deviceGridDash" class="device-card-grid">
)rawliteral";

  // ===== Dynamic Device List from ESP32 C++ Array =====
  for (int i = 0; i < deviceCount; i++) {
    html += "<div class='device-card'>";
    html += "<div class='device-icon'><i class='fa-solid fa-plug'></i></div>";
    html += "<div class='device-name'>" + devices[i].name + "</div>";

    // Toggle Switch
    html += "<label class='switch'>";
    html += "<input type='checkbox' onchange=\"toggleGPIO(" + String(devices[i].gpio) + ", this.checked ? 'on' : 'off')\" ";
    if (devices[i].state) html += "checked";
    html += "><span class='slider'></span></label>";

    // Remove Button
    html += "<button class='remove-btn' onclick=\"location.href='/remove?gpio=" + String(devices[i].gpio) + "'\">";
    html += "<i class='fa-solid fa-trash'></i></button>";

    html += "</div>";
  }

  html += R"rawliteral(
      </div>
      <div id="emptyState" class="empty-state" style="display:none;">
        <i class="fa-regular fa-lightbulb"></i>
        <span>No devices yet.<br />Tap the + button to add.</span>
      </div>
    </div>
  </section>

  <!-- Settings / Temp / Auth Sections -->
  <section id="settings" class="section">
    <div class="card">
      <div class="card-title">Device Settings</div>
      <!-- Example WiFi toggle -->
      <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:8px;">
        <span>Wi‑Fi</span>
        <label class="switch">
          <input type="checkbox" id="wifiToggle" checked>
          <span class="slider"></span>
        </label>
      </div>
    </div>
  </section>

  <section id="temperature" class="section">
    <div class="card">
      <div class="card-title">Room Temperature</div>
      <h2 id="tempValue">-- °C</h2>
      <p id="tempStatus">Reading sensor…</p>
    </div>
  </section>

  <section id="auth" class="section">
    <div class="card">
      <div class="card-title">Forgot Password</div>
      <form action="/reset" method="get">
        <input type="password" name="devicePwd" placeholder="Device Password"><br><br>
        <input type="password" name="wifiPwd" placeholder="Wi‑Fi Password"><br><br>
        <button class="btn" type="submit">Reset</button>
      </form>
    </div>
  </section>
</main>

<!-- Add Device Floating Button -->
<button class="fab" onclick="document.getElementById('addModal').classList.add('open')">
  <i class="fa-solid fa-plus"></i>
</button>

<!-- Bottom Navigation -->
<nav>
  <button id="navDashboard" class="active" onclick="showSection('dashboard')"><i class="fa-solid fa-house"></i><span>Home</span></button>
  <button id="navSettings" onclick="showSection('settings')"><i class="fa-solid fa-gear"></i><span>Settings</span></button>
  <button id="navTemp" onclick="showSection('temperature')"><i class="fa-solid fa-temperature-three-quarters"></i><span>Temp</span></button>
  <button id="navAuth" onclick="showSection('auth')"><i class="fa-solid fa-user-lock"></i><span>Auth</span></button>
</nav>

<!-- Add Device Modal -->
<div id="addModal" class="modal" onclick="closeModal()">
  <div class="modal-content" onclick="event.stopPropagation()">
    <h2>Add Device</h2>
    <form action="/add" method="get">
      <input name="name" type="text" placeholder="Device Name" required><br><br>
      <input name="gpio" type="number" placeholder="GPIO Pin" required><br><br>
      <button class="btn" type="submit"><i class="fa-solid fa-plus"></i> Add</button>
    </form>
  </div>
</div>

<script>
function toggleGPIO(gpio, state) {
  fetch('/' + state + '?gpio=' + gpio).then(() => location.reload());
}
function closeModal() {
  document.getElementById('addModal').classList.remove('open');
}
function showSection(id) {
  document.querySelectorAll('.section').forEach(s => s.classList.toggle('active', s.id === id));
  document.querySelectorAll('nav button').forEach(b => b.classList.toggle('active', b.id === 'nav' + id.charAt(0).toUpperCase() + id.slice(1)));
}
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}




void handleAddDevice() {
  if (deviceCount >= MAX_DEVICES) {
    server.send(200, "text/plain", "Max device limit reached!");
    return;
  }
  String name = server.arg("name");
  int gpio = server.arg("gpio").toInt();
  if (name == "" || gpio < 0 || gpio > 39) {
    server.send(200, "text/plain", "Invalid input!");
    return;
  }
  devices[deviceCount].name = name;
  devices[deviceCount].gpio = gpio;
  devices[deviceCount].state = false;
  pinMode(gpio, OUTPUT);
  digitalWrite(gpio, LOW);
  deviceCount++;
  saveDevices();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleRemoveDevice() {
  int gpio = server.arg("gpio").toInt();
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].gpio == gpio) {
      for (int j = i; j < deviceCount - 1; j++) devices[j] = devices[j + 1];
      deviceCount--;
      saveDevices();
      break;
    }
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleNotFound() {
  String path = server.uri();
  if (path.startsWith("/on")) {
    int gpio = server.arg("gpio").toInt();
    digitalWrite(gpio, HIGH);
    updateDeviceState(gpio, true);
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else if (path.startsWith("/off")) {
    int gpio = server.arg("gpio").toInt();
    digitalWrite(gpio, LOW);
    updateDeviceState(gpio, false);
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void updateDeviceState(int gpio, bool state) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].gpio == gpio) {
      devices[i].state = state;
      drawDeviceButton(i);  // Redraw the specific button with new state
      break;
    }
  }
  saveDevices();
}


void handleSensorData() {
  DynamicJsonDocument doc(256);
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void startWebServer() {
  server.on("/", handleRoot);
  server.on("/get_sensor", handleSensorData);
  server.on("/add", handleAddDevice);
  server.on("/remove", handleRemoveDevice);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started.");
}

void setup() {
  Serial.begin(115200);

  dht.begin();
  pinMode(DHTPIN, INPUT);
  loadDevices();

  bool wifiConnected = false;
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    //displayStatus("WiFi: " + WiFi.localIP().toString());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Smart_Switch_Pro 🛜 ", "12345678");
    IPAddress myIP = WiFi.softAPIP();
    //displayStatus("AP Mode: " + myIP.toString());
  }

  if (wifiConnected && checkForUpdate()) {
    downloadFirmware(firmwareURL);
  }

  startWebServer();

  tft.init();
  tft.setRotation(1);
  tft.setTouch(calData);  // Apply touch calibration
  tft.fillScreen(TFT_BLACK);

  // Init LED pins
  for (int i = 0; i < deviceCount; i++) {
  pinMode(devices[i].gpio, OUTPUT);
  digitalWrite(devices[i].gpio, devices[i].state ? HIGH : LOW);
}

  drawButtons();
  tft.fillScreen(TFT_BLACK);
drawWiFiStatus();  // Show Wi-Fi icon and IP address
loadDevices();
drawButtons();  // Always draw after loading


}

void loop() {
  uint16_t x, y;
  if (tft.getTouch(&x, &y)) {
    int btn = getButton(x, y); // ✅ Declare here
    if (btn != -1) {
      devices[btn].state = !devices[btn].state;
      digitalWrite(devices[btn].gpio, devices[btn].state ? HIGH : LOW);
      drawDeviceButton(btn);
      saveDevices();
    }
    delay(300);  // Debounce
  }

  server.handleClient();

  static unsigned long lastDHT = 0;
  if (millis() - lastDHT > 5000) {
    lastDHT = millis();
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    Serial.printf("Temp: %.1f°C  Hum: %.1f%%\n", temperature, humidity);
  }
}


void drawButtons() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < deviceCount; i++) {
    drawDeviceButton(i);
  }
}
void drawDeviceButton(int index) {
  int col = (index % 2);
  int row = (index / 2);
  int x = FRAME_X + col * BUTTON_W;
  int y = FRAME_Y + row * BUTTON_H;

  bool state = devices[index].state;

  tft.fillRect(x, y, BUTTON_W - 2, BUTTON_H - 2, state ? TFT_GREEN : TFT_RED);
  tft.drawRect(x, y, BUTTON_W - 2, BUTTON_H - 2, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, state ? TFT_GREEN : TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  String label = devices[index].name;
  tft.drawString(label, x + (BUTTON_W / 2) - 2, y + (BUTTON_H / 2) - 2);
}



void updateButton(int index) {
  int col = (index % 2);
  int row = (index / 2);
  int x = FRAME_X + col * BUTTON_W;
  int y = FRAME_Y + row * BUTTON_H;

  bool state = devices[index].state;  // ✅ Declare once

  tft.fillRect(x, y, BUTTON_W - 2, BUTTON_H - 2, state ? TFT_GREEN : TFT_RED);
  tft.drawRect(x, y, BUTTON_W - 2, BUTTON_H - 2, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, state ? TFT_GREEN : TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  String label = devices[index].name;
  tft.drawString(label, x + (BUTTON_W / 2) - 2, y + (BUTTON_H / 2) - 2);
}


void drawWiFiStatus() {
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (WiFi.status() == WL_CONNECTED) {
    tft.drawString("WiFi", 5, 5); 
    tft.drawString(WiFi.localIP().toString(), 40, 5);
  } else {
    tft.drawString("D Hotspot", 5, 5);
    tft.drawString("192.168.4.1", 60, 5); 
  }
}



int getButton(int x, int y) {
  for (int i = 0; i < deviceCount; i++) {
    int col = i % 2;
    int row = i / 2;
    int bx = FRAME_X + col * BUTTON_W;
    int by = FRAME_Y + row * BUTTON_H;
    if (x > bx && x < bx + BUTTON_W && y > by && y < by + BUTTON_H) {
      return i;
    }
  }
  return -1;
}