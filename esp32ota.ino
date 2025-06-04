#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

// WiFi and OTA
const char* defaultSSID = "admin";
const char* defaultPassword = "password";
const char* versionURL = "https://raw.githubusercontent.com/IT-DIVIDMARK/esp32ota/main/version.json";
String firmwareURL;
#define FIRMWARE_VERSION "1.0.6"

// Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// LED and Preferences
const int ledPin = 2;
Preferences preferences;

// Web Portal
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
const char* apSSID = "ESP-Setup";
String networksList = "";

void scanNetworks() {
  int n = WiFi.scanNetworks();
  networksList = "";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    networksList += "<option value='" + ssid + "'>" + ssid + "</option>";
  }
}

void handleRoot() {
  scanNetworks();
  String html = "<html><body><h2>Select WiFi</h2><form action='/save' method='post'>SSID:<select name='ssid'>" + networksList + "</select><br>Password:<input name='pass' type='password'/><br><input type='submit' value='Connect'/></form></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  saveWiFiCredentials(ssid, pass);
  server.send(200, "text/html", "<html><body><h2>Saved. Rebooting...</h2></body></html>");
  delay(2000);
  ESP.restart();
}

void startWiFiPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID);
  IPAddress apIP(192, 168, 4, 1);
  dnsServer.start(DNS_PORT, "*", apIP);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("AP: ESP-Setup");
  display.println("Go to: 192.168.4.1");
  display.display();
  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(10);
  }
}

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

bool checkForUpdate() {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(versionURL);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload)) return false;
    String latestVersion = doc["version"];
    firmwareURL = doc["firmware"].as<String>();
    if (latestVersion != FIRMWARE_VERSION) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("OTA Update Available");
      display.display();
      return true;
    }
  }
  return false;
}

void downloadFirmware(String url) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    if (Update.begin(contentLength)) {
      WiFiClient& client = http.getStream();
      if (Update.writeStream(client) == contentLength && Update.end()) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Update Success!");
        display.display();
        delay(2000);
        ESP.restart();
      }
    }
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 failed");
    while (1);
  }
  displayFirmwareVersion();

  WiFi.begin(defaultSSID, defaultPassword);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    tries++;
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    String savedSSID, savedPassword;
    if (!loadWiFiCredentials(savedSSID, savedPassword) || WiFi.begin(savedSSID.c_str(), savedPassword.c_str()), WiFi.status() != WL_CONNECTED) {
      startWiFiPortal();
    }
  }

  if (WiFi.status() == WL_CONNECTED && checkForUpdate()) {
    downloadFirmware(firmwareURL);
  }
}

void loop() {
  static unsigned long previousMillis = 0;
  if (millis() - previousMillis >= 1000) {
    previousMillis = millis();
    digitalWrite(ledPin, !digitalRead(ledPin));
    display.clearDisplay();
    displayFirmwareVersion();
    display.setCursor(0, 50);
    display.println("Running Loop...");
    display.display();
  }
}
