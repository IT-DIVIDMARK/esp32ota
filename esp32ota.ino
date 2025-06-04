#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
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

// Define LED Pin (On-board LED, GPIO 2)
const int ledPin = 2;

// Current Firmware Version
#define FIRMWARE_VERSION "1.0.11"

// Display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Preferences instance to store Wi-Fi credentials
Preferences preferences;

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

// WiFi Connect Function
void connectToWiFi(String newSSID, String newPassword) {
  WiFi.begin(newSSID.c_str(), newPassword.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {  // Retry for 30 seconds
    delay(500);
    attempts++;
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi!");
    display.setCursor(0, 50);
    display.println("WiFi Connected!");
    display.display();
  } else {
    Serial.println("Failed to connect to WiFi.");
    display.setCursor(0, 50);
    display.println("WiFi Connect Failed!");
    display.display();
  }
}

// Save WiFi credentials in Preferences
void saveWiFiCredentials(String ssid, String password) {
  preferences.begin("wifi", false);  // "wifi" namespace
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
}

// Load WiFi credentials from Preferences
bool loadWiFiCredentials(String &ssid, String &password) {
  preferences.begin("wifi", true);  // "wifi" namespace (read-only)
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();

  return ssid.length() > 0 && password.length() > 0;
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

  // Try to connect to default WiFi
  WiFi.begin(defaultSSID, defaultPassword);
  int connectAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && connectAttempts < 20) {  // Wait for 10 seconds
    delay(500);
    connectAttempts++;
    Serial.print(".");
    display.setCursor(0, 50);
    display.println("Connecting WiFi...");
    display.display();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi!");
    display.setCursor(0, 50);
    display.println("WiFi Connected!");
    display.display();
    delay(2000);

    // Check for Update Only Once
    if (checkForUpdate()) {
      downloadFirmware(firmwareURL);
    }
  } else {
    Serial.println("Failed to connect to default WiFi.");
    display.setCursor(0, 50);
    display.println("WiFi Connect Failed!");
    display.display();
    delay(2000);

    // Try to connect to saved Wi-Fi credentials
    String savedSSID, savedPassword;
    if (loadWiFiCredentials(savedSSID, savedPassword)) {
      Serial.println("Trying to connect to saved WiFi...");
      connectToWiFi(savedSSID, savedPassword);
      if (WiFi.status() == WL_CONNECTED) {
        // Check for OTA update
        if (checkForUpdate()) {
          downloadFirmware(firmwareURL);
        }
      }
    } else {
      Serial.println("No saved Wi-Fi credentials found.");
      display.setCursor(0, 50);
      display.println("No WiFi Credentials!");
      display.display();
      delay(2000);

      // Prompt user for new Wi-Fi credentials
      String newSSID, newPassword;
      Serial.println("Enter SSID: ");
      while (Serial.available() == 0) {}  // Wait for SSID input
      newSSID = Serial.readStringUntil('\n');
      
      Serial.println("Enter Password: ");
      while (Serial.available() == 0) {}  // Wait for password input
      newPassword = Serial.readStringUntil('\n');

      // Trim any unnecessary newline or spaces
      newSSID.trim();
      newPassword.trim();

      // Save credentials
      saveWiFiCredentials(newSSID, newPassword);

      // Connect to the new Wi-Fi network
      connectToWiFi(newSSID, newPassword);

      // After connection, check for OTA update
      if (WiFi.status() == WL_CONNECTED && checkForUpdate()) {
        downloadFirmware(firmwareURL);
      }
    }
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
