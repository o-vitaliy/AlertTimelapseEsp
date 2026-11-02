#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_bt.h>

// ESP32-C3 Super Mini pins
#define TFT_CS   10
#define TFT_DC   2
#define TFT_RST  3
#define TFT_SCK  4
#define TFT_MOSI 5

constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;

unsigned long lastWiFiAttempt = 0;
bool wasWiFiConnected = false;

// Display object
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);

  // Initialize SPI
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

  // Initialize 2.0 inch ST7789
  tft.init(240, 320);

  // Landscape orientation
  tft.setRotation(1);

  // Clear screen
  tft.fillScreen(ST77XX_BLACK);

  // Display title
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(3);
  tft.setCursor(65, 75);
  tft.println("HELLO");

  // Display World
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(65, 115);
  tft.println("WORLD!");

  // Display ESP32 information
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(55, 180);
  tft.println("ESP32-C3");

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(55, 210);
  tft.println("Super Mini");

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  Serial.println("Connect to the AlertScreen-Setup Wi-Fi network to choose your Wi-Fi.");

  if (!wifiManager.autoConnect("AlertScreen-Setup")) {
    Serial.println("Wi-Fi setup timed out; restarting to try again.");
    ESP.restart();
  }

  esp_bt_controller_disable();
  WiFi.setAutoReconnect(true);
  lastWiFiAttempt = millis();
  Serial.print("Wi-Fi connected. IP address: ");
  Serial.println(WiFi.localIP());
  wasWiFiConnected = true;
}

void loop() {
  const bool isWiFiConnected = WiFi.status() == WL_CONNECTED;

  if (isWiFiConnected && !wasWiFiConnected) {
    Serial.print("Wi-Fi connected. IP address: ");
    Serial.println(WiFi.localIP());
  } else if (!isWiFiConnected && wasWiFiConnected) {
    Serial.println("Wi-Fi disconnected.");
  }
  wasWiFiConnected = isWiFiConnected;

  if (!isWiFiConnected && millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL_MS) {
    Serial.println("Retrying Wi-Fi connection...");
    WiFi.reconnect();
    lastWiFiAttempt = millis();
  }
}