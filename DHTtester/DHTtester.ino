#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "DHT.h"

// WiFi credentials
const char* ssid = "admin";
const char* password = "password";

// DHT22 setup
#define DHTPIN 18         // Change this GPIO pin according to your DHT22 connection
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

void loadDevices();
void saveDevices();
void updateDeviceState(int gpio, bool state);
void handleRoot();
void handleSensorData();
void handleAddDevice();
void handleRemoveDevice();
void handleNotFound();

WebServer server(80);
Preferences preferences;

struct Device {
  String name;
  int gpio;
  bool state;
};

#define MAX_DEVICES 4
Device devices[MAX_DEVICES];
int deviceCount = 0;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());

  dht.begin();

  preferences.begin("devices", false);
  loadDevices();

  server.on("/", handleRoot);
  server.on("/get_sensor", handleSensorData);
  server.on("/add", handleAddDevice);
  server.on("/remove", handleRemoveDevice);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Smart Dashboard</title>
  <style>
    body { font-family: Arial; background: #f0f0f0; text-align: center; margin: 0; padding: 20px; }
    .dashboard { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; }
    .card { background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
    h2 { margin-bottom: 10px; }
    .temp { font-size: 2em; color: #ff5722; }
    .switch { margin-top: 10px; }
    .toggle-btn { position: relative; display: inline-block; width: 50px; height: 24px; }
    .toggle-btn input { display: none; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 24px; transition: 0.4s; }
    .slider:before { position: absolute; content: ""; height: 20px; width: 20px; left: 2px; bottom: 2px; background-color: white; border-radius: 50%; transition: 0.4s; }
    input:checked + .slider { background-color: #4caf50; }
    input:checked + .slider:before { transform: translateX(26px); }
    a.remove-btn { color: red; margin-left: 10px; text-decoration: none; }
  </style>
  <script>
    function updateSensor() {
      fetch('/get_sensor')
        .then(response => response.json())
        .then(data => {
          document.getElementById('tempValue').innerText = data.temperature + '°C';
        });
    }
    setInterval(updateSensor, 5000);
    window.onload = updateSensor;

    function toggleGPIO(gpio, state) {
      fetch('/' + state + '?gpio=' + gpio)
        .then(() => location.reload());
    }
  </script>
</head>
<body>
  <h1> ESP32 Smart Home Dashboard</h1>
  <div class="dashboard">

    <!-- Temperature Card -->
    <div class="card">
      <h2>🌡 Temperature</h2>
      <div class="temp" id="tempValue">-- °C</div>
    </div>
)rawliteral";

  // Generate a card for each device
  for (int i = 0; i < deviceCount; i++) {
    html += "<div class='card'>";
    html += "<h2>" + devices[i].name + "</h2>";

    html += "<div class='switch'>";
    html += "<span>GPIO " + String(devices[i].gpio) + "</span><br>";

    // Toggle Switch
    html += "<label class='toggle-btn'>";
    html += "<input type='checkbox' onchange=\"toggleGPIO(" + String(devices[i].gpio) + ", this.checked ? 'on' : 'off')\" ";
    if (devices[i].state) html += "checked";
    html += ">";
    html += "<span class='slider'></span>";
    html += "</label>";

    // Remove Button
    html += "<br><a href='/remove?gpio=" + String(devices[i].gpio) + "' class='remove-btn'>Remove</a>";

    html += "</div></div>";
  }

  // Device Add Form Card
  html += R"rawliteral(
    <div class="card">
      <h2>Add New Device</h2>
      <form action="/add" method="get">
        <input type="text" name="name" placeholder="Device Name"><br><br>
        <input type="number" name="gpio" placeholder="GPIO Pin"><br><br>
        <input type="submit" value="Add Device">
      </form>
    </div>
  )rawliteral";

  html += "</div></body></html>";

  server.send(200, "text/html", html);
}


void handleSensorData() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  DynamicJsonDocument doc(256);
  if (isnan(temp) || isnan(hum)) {
    doc["temperature"] = "Error";
    doc["humidity"] = "Error";
  } else {
    doc["temperature"] = temp;
    doc["humidity"] = hum;
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAddDevice() {
  if (deviceCount >= MAX_DEVICES) {
    server.send(200, "text/plain", "Device limit reached!");
    return;
  }

  String name = server.arg("name");
  int gpio = server.arg("gpio").toInt();

  if (name.length() == 0 || gpio < 0 || gpio > 39) {
    server.send(200, "text/plain", "Invalid name or GPIO pin!");
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
  bool found = false;

  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].gpio == gpio) {
      // Shift remaining devices down
      for (int j = i; j < deviceCount - 1; j++) {
        devices[j] = devices[j + 1];
      }
      deviceCount--;
      found = true;
      break;
    }
  }

  if (found) {
    saveDevices();
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(200, "text/plain", "Device not found!");
  }
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
    server.send(404, "text/plain", "Not found!");
  }
}

void updateDeviceState(int gpio, bool state) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].gpio == gpio) {
      devices[i].state = state;
      break;
    }
  }
}

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
  preferences.putString("config", jsonString);
}

void loadDevices() {
  String jsonString = preferences.getString("config", "");

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

void loop() {
  server.handleClient();
}
