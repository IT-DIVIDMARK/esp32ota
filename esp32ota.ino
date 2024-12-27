#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DFRobot_ID809.h>
#include <HTTPClient.h>  // Add this library for HTTP requests
#include <Update.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "admin";
const char* password = "password";

// URLs for OTA update and version check
const char* versionURL = "https://raw.githubusercontent.com/IT-DIVIDMARK/esp32ota/main/version.json";
String firmwareURL;

// Define pins for RX and TX for Fingerprint Sensor
#define RX_PIN 16  // Connect this to the TX pin of the fingerprint sensor
#define TX_PIN 17  // Connect this to the RX pin of the fingerprint sensor

// I2C Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Use Hardware Serial1 for ESP32
#define FPSerial Serial1
DFRobot_ID809 fingerprint;
WebServer server(80);  // Initialize web server on port 80

String resultMessage = "";  // Message to display on the web page
bool manualMode = false;    // Flag to detect if manual mode is active

#define FIRMWARE_VERSION "1.0.1"
const int ledPin = 2;  // On-board LED pin

// Function declarations
bool checkForUpdate();
void downloadFirmware(String url);
String getHTML();
void storeFingerprint();
void matchFingerprint();
void sendFingerprintToCloud(uint8_t fingerprintID);

void setup() {
  Serial.begin(115200);

  // Initialize I2C display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Initialize Fingerprint Sensor
  FPSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);  // Set RX and TX pins
  fingerprint.begin(FPSerial);

  while (!Serial);

  while (!fingerprint.isConnected()) {
    Serial.println("Communication with device failed, please check connection");
    delay(1000);
  }

  // Check for OTA update before connecting to WiFi
  if (checkForUpdate()) {
    downloadFirmware(firmwareURL);
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Display IP Address on I2C Display
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);  // Increase text size
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();

  // Setup web server routes
  server.on("/", HTTP_GET, []() {
    String html = getHTML();
    server.send(200, "text/html", html);
  });

  server.begin();
}

void loop() {
  server.handleClient();  // Handle incoming web requests

  // Automatically match fingerprint if a finger is detected
  if (fingerprint.detectFinger()) {
    matchFingerprint();
    while (fingerprint.detectFinger());  // Wait for the finger to be removed
    delay(1000);  // Short delay before the next detection
    return;  // Skip manual option check for this iteration
  }

  // Manual mode to select storing or matching fingerprint
  if (Serial.available()) {
    int option = Serial.read() - '0';  // Read user input and convert char to int
    switch (option) {
      case 1:
        storeFingerprint();
        break;
      case 2:
        matchFingerprint();
        break;
      default:
        Serial.println("Invalid option. Please select 1 (Store Fingerprint) or 2 (Match Fingerprint).");
        break;
    }
  }

  delay(1000);  // Short delay before checking the next action
}

void storeFingerprint() {
  uint8_t ID, i;

  if ((ID = fingerprint.getEmptyID()) == ERR_ID809) {
    resultMessage = "No empty ID available";
    Serial.println(resultMessage);
    return;
  }

  Serial.print("Unregistered ID, ID=");
  Serial.println(ID);

  i = 0;  // Clear sampling times
  while (i < 3) {
    fingerprint.ctrlLED(fingerprint.eBreathing, fingerprint.eLEDBlue, 0);
    Serial.print("Taking fingerprint sample number ");
    Serial.println(i + 1);
    Serial.println("Please press down your finger");

    if ((fingerprint.collectionFingerprint(0)) != ERR_ID809) {
      fingerprint.ctrlLED(fingerprint.eFastBlink, fingerprint.eLEDYellow, 3);
      Serial.println("Sampling succeeded");
      i++;
    } else {
      Serial.println("Sampling failed");
    }

    while (fingerprint.detectFinger());
  }

  if (fingerprint.storeFingerprint(ID) != ERR_ID809) {
    resultMessage = "Saving succeeded, ID=" + String(ID);
    Serial.println(resultMessage);
    fingerprint.ctrlLED(fingerprint.eKeepsOn, fingerprint.eLEDGreen, 0);
  } else {
    resultMessage = "Saving failed";
    Serial.println(resultMessage);
  }

  fingerprint.ctrlLED(fingerprint.eNormalClose, fingerprint.eLEDBlue, 0);
}

void matchFingerprint() {
  uint8_t ret = 0;
  fingerprint.ctrlLED(fingerprint.eBreathing, fingerprint.eLEDBlue, 0);
  Serial.println("Please press down your finger");

  if ((fingerprint.collectionFingerprint(0)) != ERR_ID809) {
    fingerprint.ctrlLED(fingerprint.eFastBlink, fingerprint.eLEDYellow, 3);
    Serial.println("Capturing succeeds");
    Serial.println("Please release your finger");

    while (fingerprint.detectFinger());

    ret = fingerprint.search();

    if (ret != 0) {
      resultMessage = "Matching succeeds, ID=" + String(ret);
      Serial.println(resultMessage);
      fingerprint.ctrlLED(fingerprint.eKeepsOn, fingerprint.eLEDGreen, 0);
      sendFingerprintToCloud(ret);
    } else {
      resultMessage = "Matching fails";
      Serial.println(resultMessage);
      fingerprint.ctrlLED(fingerprint.eKeepsOn, fingerprint.eLEDRed, 0);
    }
  } else {
    resultMessage = "Capturing fails";
    Serial.println(resultMessage);
  }

  fingerprint.ctrlLED(fingerprint.eNormalClose, fingerprint.eLEDBlue, 0);
}

void sendFingerprintToCloud(uint8_t fingerprintID) {
  HTTPClient http;
  String url = "http://192.168.92.186:8000/fingerprint/" + String(fingerprintID);
  
  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Server Response: " + response);
  } else {
    Serial.println("Error on sending GET: " + String(httpResponseCode));
  }

  http.end();
}

String getHTML() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Fingerprint System</title>";
  html += "</head><body><h1>Fingerprint Result</h1>";
  html += "<p>" + resultMessage + "</p>";
  html += "</body></html>";
  return html;
}

// OTA Update Functions
bool checkForUpdate() {
  HTTPClient http;
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
      return true;
    }
  }
  http.end();
  return false;
}

void downloadFirmware(String url) {
  HTTPClient http;
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
        delay(1000);
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
