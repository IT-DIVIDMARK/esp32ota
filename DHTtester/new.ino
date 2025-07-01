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
#define FIRMWARE_VERSION "1.0.17"

// OLED Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// DHT22 Sensor
#define DHTPIN 18
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

// Function Prototypes
void handleRoot();
void handleAddDevice();
void handleRemoveDevice();
void handleNotFound();
void handleSensorData();
void loadDevices();
void saveDevices();
void updateDeviceState(int gpio, bool state);
void displayStatus(String status);
void startWebServer();
void downloadFirmware(String url);
bool checkForUpdate();

// Display status
void displayStatus(String status) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("FW Ver: " FIRMWARE_VERSION);
  display.setCursor(0, 15);
  display.println(status);
  display.display();
}

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
        displayStatus("Updated! Rebooting...");
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

// Web Pages
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Smart Home</title>
  <style>
    body { font-family: Arial; background: #f0f0f0; text-align: center; }
    .dashboard { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 20px; }
    .card { background: white; padding: 15px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
    .temp { font-size: 2em; color: #ff5722; }
    .switch { margin-top: 10px; }
    .toggle-btn { position: relative; display: inline-block; width: 50px; height: 24px; }
    .toggle-btn input { display: none; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 24px; transition: .4s; }
    .slider:before { position: absolute; content: ''; height: 20px; width: 20px; left: 2px; bottom: 2px; background-color: white; border-radius: 50%; transition: .4s; }
    input:checked + .slider { background-color: #4caf50; }
    input:checked + .slider:before { transform: translateX(26px); }
    a.remove-btn { color: red; text-decoration: none; margin-left: 10px; }
  </style>
  <script>
    function updateSensor() {
      fetch('/get_sensor')
      .then(response => response.json())
      .then(data => {
        document.getElementById('temp').innerText = data.temperature + '°C';
        document.getElementById('hum').innerText = data.humidity + '%';
      });
    }
    setInterval(updateSensor, 5000);
    window.onload = updateSensor;

    function toggleGPIO(gpio, state) {
      fetch('/' + state + '?gpio=' + gpio).then(() => location.reload());
    }
  </script>
</head>
<body>
  <h1>🏠 ESP32 Smart Dashboard</h1>
  <div class="dashboard">

    <div class="card">
      <h2>🥵 Temperature</h2>
      <div class="temp" id="temp">-- °C</div>
      <h2>💧 Humidity</h2>
      <div class="temp" id="hum">-- %</div>
    </div>
)rawliteral";

  // Device Cards
  for (int i = 0; i < deviceCount; i++) {
    html += "<div class='card'>";
    html += "<h2>" + devices[i].name + "</h2>";
    html += "<div>GPIO: " + String(devices[i].gpio) + "</div>";
    html += "<label class='toggle-btn'>";
    html += "<input type='checkbox' onchange=\"toggleGPIO(" + String(devices[i].gpio) + ", this.checked ? 'on' : 'off')\" ";
    if (devices[i].state) html += "checked";
    html += "><span class='slider'></span></label>";
    html += "<br><a href='/remove?gpio=" + String(devices[i].gpio) + "' class='remove-btn'>Remove</a>";
    html += "</div>";
  }

  // Add Device Form
  html += R"rawliteral(
    <div class="card">
      <h2>Add Device</h2>
      <form action="/add" method="get">
        <input type="text" name="name" placeholder="Device Name"><br><br>
        <input type="number" name="gpio" placeholder="GPIO Pin"><br><br>
        <input type="submit" value="Add">
      </form>
    </div>
  )rawliteral";

  html += "</div></body></html>";
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
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) while (1);
  displayStatus("Booting...");

  dht.begin();
  pinMode(DHTPIN, INPUT);
  loadDevices();

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    displayStatus("WiFi: " + WiFi.localIP().toString());
    if (checkForUpdate()) downloadFirmware(firmwareURL);
    startWebServer();
  } else {
    displayStatus("WiFi Failed");
  }
}

void loop() {
  server.handleClient();

  static unsigned long lastDHT = 0;
  if (millis() - lastDHT > 5000) {
    lastDHT = millis();
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    Serial.printf("Temp: %.1f°C  Hum: %.1f%%\n", temperature, humidity);
  }
}
