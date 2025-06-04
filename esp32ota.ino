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
#define FIRMWARE_VERSION "1.0.10"

// Display config
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Preferences instance
Preferences preferences;

// Web server instance
WebServer server(80);

// Web page handlers
void handleRoot() {
  String html = "<html><head><title>ESP32 LED Control</title></head><body>";
  html += "<h1>LED Control</h1>";
  html += "<p><a href=\"/led/on\"><button>Turn LED ON</button></a></p>";
  html += "<p><a href=\"/led/off\"><button>Turn LED OFF</button></a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleLEDOn() {
  digitalWrite(ledPin, HIGH);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleLEDOff() {
  digitalWrite(ledPin, LOW);
  server.sendHeader("Location", "/");
  server.send(303);
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
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Firmware Updated!");
        display.display();
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

void displayFirmwareVersion() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Firmware Version:");
  display.setTextSize(2);
  display.println(FIRMWARE_VERSION);
  display.display();
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
    display.setCursor(0, 50);
    display.println("WiFi Connected!");
    display.display();
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

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (1);
  }

  displayFirmwareVersion();

  WiFi.begin(defaultSSID, defaultPassword);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0, 50);
    display.println(WiFi.localIP());
    display.display();
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
    }
  }
}

void loop() {
  server.handleClient();
} 
