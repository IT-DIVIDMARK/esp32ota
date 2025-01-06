#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// WiFi credentials
const char* ssid = "admin";
const char* password = "password";

// URLs
const char* versionURL = "https://raw.githubusercontent.com/IT-DIVIDMARK/esp32ota/main/version.json";
String firmwareURL;

// Define LED Pin (On-board LED, GPIO 2)
const int ledPin = 2;

// Current Firmware Version
#define FIRMWARE_VERSION "1.0.2"

// Display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

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

// Setup Function
void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);

  // Initialize Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Default I2C address 0x3C
    Serial.println("SSD1306 allocation failed");
    while (1);
  }
  Serial.println("Display initialized");

  displayFirmwareVersion();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(ledPin, !digitalRead(ledPin)); // Blink LED while connecting
    delay(500);
    Serial.println("Connecting to WiFi...");
    display.setCursor(0, 50);
    display.println("Connecting WiFi...");
    display.display();
  }
  Serial.println("Connected to WiFi!");
  display.setCursor(0, 50);
  display.println("WiFi Connected!");
  display.display();
  delay(2000);

  // Check for Update Only Once
  if (checkForUpdate()) {
    downloadFirmware(firmwareURL);
  }
}

// Main Loop
void loop() {
  // Blink onboard LED every 1 second
  static unsigned long previousMillis = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= 1000) {
    previousMillis = currentMillis;
    digitalWrite(ledPin, !digitalRead(ledPin));
    Serial.println("Running main loop...");
    display.clearDisplay();
    displayFirmwareVersion();
    display.setCursor(0, 50);
    display.println("Running Loop...");
    display.display();
  }
}
