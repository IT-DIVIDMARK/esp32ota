#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

const char* ssid = "admin";
const char* password = "password";

WebServer server(80);
Preferences preferences;

struct Device {
  String name;
  int gpio;
  bool state;
};

#define MAX_DEVICES 10
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

  preferences.begin("devices", false);
  loadDevices();

  server.on("/", handleRoot);
  server.on("/add", handleAddDevice);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleRoot() {
  String html = "<html><body><h2>ESP32 GPIO Device Config</h2>";

  html += "<h3>Add New Device:</h3>";
  html += "<form action='/add' method='get'>";
  html += "Device Name: <input type='text' name='name'><br>";
  html += "GPIO Pin: <input type='number' name='gpio'><br>";
  html += "<input type='submit' value='Add Device'>";
  html += "</form><hr>";

  html += "<h3>Control Devices:</h3>";
  for (int i = 0; i < deviceCount; i++) {
    html += "<p>" + devices[i].name + " (GPIO " + String(devices[i].gpio) + ")";
    html += " - <a href='/on?gpio=" + String(devices[i].gpio) + "'>ON</a> | ";
    html += "<a href='/off?gpio=" + String(devices[i].gpio) + "'>OFF</a></p>";
  }

  html += "</body></html>";
  server.send(200, "text/html", html);
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
