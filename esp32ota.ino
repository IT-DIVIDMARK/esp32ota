#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <Preferences.h>

// WiFi credentials
const char* defaultSSID = "admin";
const char* defaultPassword = "password";

// URLs
const char* versionURL = "https://raw.githubusercontent.com/IT-DIVIDMARK/esp32ota/main/version.json";
String firmwareURL;

// Pins
const int ledPin = 4;

// Firmware version
#define FIRMWARE_VERSION "1.0.9"

// OLED Display config
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Preferences
Preferences preferences;

// WebServer
WebServer server(80);

// Web Interface HTML
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
  <head><title>ESP32 LED Control</title></head>
  <body style="text-align:center;">
    <h2>LED Control Panel</h2>
    <button onclick="fetch('/on')">LED ON</button>
    <button onclick="fetch('/off')">LED OFF</button>
    <button onclick="fetch('/toggle')">TOGGLE LED</button>
  </body>
</html>
)rawliteral";

// OTA Firmware Download
void downloadFirmware(String url) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.print("Firmware Size: ");
    Serial.println(contentLength);

    if (contentLength <= 0) {
      Serial.println("Invalid firmware size");
      return;
    }

    bool canBegin = Update.begin(contentLength);
    if (canBegin) {
      WiFiClient& client = http.getStream();
      size_t written = Update.writeStream(client);

      if (written == contentLength && Update.end()) {
        Serial.println("Firmware update successful! Rebooting...");
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("Firmware Update Done!");
        display.display();
        delay(2000);
        ESP.restart();
      } else {
        Serial.print("Update failed. Written: ");
        Serial.println(written);
        Serial.println(Update.errorString());
      }
    } else {
      Serial.println("Not enough space for OTA update.");
    }
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpCode);
  }

  http.end();
}

// OTA Update Check
bool checkForUpdate() {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(versionURL);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON Parsing Error: ");
      Serial.println(error.c_str());
      return false;
    }

    String latestVersion = doc["version"];
    firmwareURL = doc["firmware"].as<String>();

    Serial.print("Current Version: ");
    Serial.println(FIRMWARE_VERSION);
    Serial.print("Latest Version: ");
    Serial.println(latestVersion);

    if (latestVersion != FIRMWARE_VERSION) {
      Serial.println("New firmware available, starting OTA...");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("OTA Update Available");
      display.display();
      return true;
    } else {
      Serial.println("Firmware is up to date.");
    }
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpCode);
  }

  http.end();
  return false;
}

// Display Firmware Version
void displayFirmwareVersion() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Firmware Version:");
  display.setTextSize(2);
  display.println(FIRMWARE_VERSION);
  display.display();
}

// Web server
void startWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlPage);
  });

  server.on("/on", HTTP_GET, []() {
    digitalWrite(ledPin, HIGH);
    server.send(200, "text/plain", "LED ON");
  });

  server.on("/off", HTTP_GET, []() {
    digitalWrite(ledPin, LOW);
    server.send(200, "text/plain", "LED OFF");
  });

  server.on("/toggle", HTTP_GET, []() {
    digitalWrite(ledPin, !digitalRead(ledPin));
    server.send(200, "text/plain", "LED TOGGLED");
  });

  server.begin();
  Serial.println("Web server started.");
}

// Save WiFi credentials
void saveWiFiCredentials(String ssid, String password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
}

// Load WiFi credentials
bool loadWiFiCredentials(String &ssid, String &password) {
  preferences.begin("wifi", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();
  return ssid.length() > 0 && password.length() > 0;
}

// Display IP address
void displayIP() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Firmware:");
  display.setTextSize(2);
  display.println(FIRMWARE_VERSION);
  display.setTextSize(1);
  display.println("Web IP:");
  display.println(WiFi.localIP());
  display.display();
}

// Connect to WiFi and setup web
void connectAndSetupWeb() {
  display.setCursor(0, 50);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(defaultSSID, defaultPassword);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Connected!");
    display.display();
    delay(1000);

    if (checkForUpdate()) {
      downloadFirmware(firmwareURL);
    }

    displayIP();
    startWebServer();
  } else {
    Serial.println("WiFi failed.");
    display.setCursor(0, 50);
    display.println("WiFi Failed!");
    display.display();
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while (1);
  }

  displayFirmwareVersion();
  connectAndSetupWeb();
}

// Loop
void loop() {
  server.handleClient();

  static unsigned long lastMillis = 0;
  if (millis() - lastMillis > 1000) {
    lastMillis = millis();
    digitalWrite(ledPin, !digitalRead(ledPin));
    Serial.println("Running loop...");
  }
}
