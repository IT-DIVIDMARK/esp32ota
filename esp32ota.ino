#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>

// WiFi credentials
const char* defaultSSID = "admin";
const char* defaultPassword = "password";

// OTA Version check URL
const char* versionURL = "https://raw.githubusercontent.com/IT-DIVIDMARK/esp32ota/main/version.json";
String firmwareURL;

// Pins
const int ledPin = 4;

// Current Firmware Version
#define FIRMWARE_VERSION "1.0.16"

// Display config
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Preferences instance
Preferences preferences;

// Web server instance
WebServer server(80);

// Function to display status
void displayStatus(String status) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Firmware Version:");
  display.setTextSize(2);
  display.println(FIRMWARE_VERSION);
  display.setTextSize(1);
  display.setCursor(0, 50);
  display.println(status);
  display.display();
}

// Web page handlers
void handleRoot() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Smart Switch Pro</title>";
  html += "<style>";
  html += "body { font-family: sans-serif; text-align: center; padding: 20px; }";
  html += ".switch { position: relative; display: inline-block; width: 60px; height: 34px; }";
  html += ".switch input { opacity: 0; width: 0; height: 0; }";
  html += ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;";
  html += "background-color: #ccc; transition: .4s; border-radius: 34px; }";
  html += ".slider:before { position: absolute; content: ''; height: 26px; width: 26px; left: 4px; bottom: 4px;";
  html += "background-color: white; transition: .4s; border-radius: 50%; }";
  html += "input:checked + .slider { background-color: #2196F3; }";
  html += "input:checked + .slider:before { transform: translateX(26px); }";
  html += "</style></head><body>";
  html += "<h1>Smart Switch Pro</h1>";

  // Add a switch for each LED
  for (int i = 1; i <= 4; i++) {
    html += "<p>LED " + String(i) + ": ";
    html += "<label class='switch'>";
    html += "<input type='checkbox' onchange='toggleLED(" + String(i) + ", this.checked)'>";
    html += "<span class='slider'></span>";
    html += "</label></p>";
  }

  html += "<script>";
  html += "function toggleLED(led, state) {";
  html += "  var xhttp = new XMLHttpRequest();";
  html += "  var url = '/led/' + led + '/' + (state ? 'on' : 'off');";
  html += "  xhttp.open('GET', url, true);";
  html += "  xhttp.send();";
  html += "}";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}



void handleLEDOn() {
  digitalWrite(ledPin, HIGH);
  server.sendHeader("Location", "/");
  server.send(303);
  displayStatus("LED ON");
}

void handleLEDOff() {
  digitalWrite(ledPin, LOW);
  server.sendHeader("Location", "/");
  server.send(303);
  displayStatus("LED OFF");
}

void startWebServer() {
  server.on("/", handleRoot);
  server.on("/led/on", handleLEDOn);
  server.on("/led/off", handleLEDOff);
  server.begin();
  Serial.println("Web server started.");
}

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
        displayStatus("Firmware Updated!");
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

void connectToWiFi(String ssid, String password) {
  WiFi.begin(ssid.c_str(), password.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    displayStatus("WiFi: " + WiFi.localIP().toString());
  }
}

void saveWiFiCredentials(String ssid, String password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
}

bool loadWiFiCredentials(String &ssid, String &password) {
  preferences.begin("wifi", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();
  return ssid.length() > 0 && password.length() > 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  Wire.begin(21, 22); // Ensure correct I2C pins for ESP32

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Display init failed");
    while (1);
  }

  displayStatus("Booting...");

  WiFi.begin(defaultSSID, defaultPassword);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    displayStatus("WiFi: " + WiFi.localIP().toString());
    if (checkForUpdate()) {
      downloadFirmware(firmwareURL);
    }
    startWebServer();
  } else {
    String savedSSID, savedPassword;
    if (loadWiFiCredentials(savedSSID, savedPassword)) {
      connectToWiFi(savedSSID, savedPassword);
      if (WiFi.status() == WL_CONNECTED) {
        if (checkForUpdate()) {
          downloadFirmware(firmwareURL);
        }
        startWebServer();
      }
    } else {
      Serial.println("No saved WiFi creds.");
      displayStatus("No WiFi creds");
    }
  }
}

void loop() {
  server.handleClient();

  static unsigned long lastDisplayUpdate = 0;
  const unsigned long displayInterval = 1000; // update every 1 seconds

  if (millis() - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = millis();

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Firmware:");
    display.setTextSize(2);
    display.println(FIRMWARE_VERSION);
    
    display.setTextSize(1);
    display.println("IP:");
    display.println(WiFi.localIP());
    
    display.display();
  }
}
