#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_bt.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>

#include "DisplayConfig.h"
#include "ContentPage.h"
#include "NoWifiPage.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;

 public:
  LGFX() {
    auto busConfig = _bus.config();
    busConfig.spi_host = SPI2_HOST;
    busConfig.spi_mode = 0;
    busConfig.freq_write = 40000000;
    busConfig.freq_read = 16000000;
    busConfig.spi_3wire = true;
    busConfig.use_lock = true;
    busConfig.dma_channel = SPI_DMA_CH_AUTO;
    busConfig.pin_sclk = TFT_SCLK;
    busConfig.pin_mosi = TFT_MOSI;
    busConfig.pin_miso = -1;
    busConfig.pin_dc = TFT_DC;
    _bus.config(busConfig);
    _panel.setBus(&_bus);

    auto panelConfig = _panel.config();
    panelConfig.pin_cs = TFT_CS;
    panelConfig.pin_rst = TFT_RST;
    panelConfig.pin_busy = -1;
    panelConfig.memory_width = TFT_NATIVE_WIDTH;
    panelConfig.memory_height = TFT_NATIVE_HEIGHT;
    panelConfig.panel_width = TFT_NATIVE_WIDTH;
    panelConfig.panel_height = TFT_NATIVE_HEIGHT;
    panelConfig.offset_x = 0;
    panelConfig.offset_y = 0;
    panelConfig.offset_rotation = 0;
    panelConfig.dummy_read_pixel = 8;
    panelConfig.dummy_read_bits = 1;
    panelConfig.readable = false;
    panelConfig.invert = TFT_INVERT;
    panelConfig.rgb_order = false;
    panelConfig.dlen_16bit = false;
    panelConfig.bus_shared = false;
    _panel.config(panelConfig);

    setPanel(&_panel);
  }
};

namespace {

constexpr uint16_t SCREEN_WIDTH = 320;
constexpr uint16_t SCREEN_HEIGHT = 240;
constexpr uint16_t BUFFER_LINES = 40;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr char WIFI_ACCESS_POINT_NAME[] = "AlertScreen-Setup";

LGFX tft;
NoWifiPage noWifiPage(WIFI_ACCESS_POINT_NAME);
ContentPage contentPage(tft);
WiFiManager wifiManager;

Page* activePage = nullptr;
uint8_t drawBuffer[SCREEN_WIDTH * BUFFER_LINES * 2];
unsigned long lastWiFiAttempt = 0;
bool wasWiFiConnected = false;
bool configPortalStarted = false;
bool displayRefreshDeferred = false;

// Sends LVGL's output to the panel, except while ContentPage owns the screen.
void displayFlush(lv_display_t* lvDisplay, const lv_area_t* area,
                  uint8_t* pixels) {
  if (contentPage.imageRefreshRunning()) {
    displayRefreshDeferred = true;
    lv_display_flush_ready(lvDisplay);
    return;
  }
  if (activePage == &contentPage && contentPage.imageDisplayed()) {
    lv_display_flush_ready(lvDisplay);
    return;
  }

  const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.pushImage(area->x1, area->y1, width, height,
                reinterpret_cast<lgfx::rgb565_t*>(pixels));
  tft.endWrite();

  lv_display_flush_ready(lvDisplay);
}

void showPage(Page& page) {
  if (activePage != &page) {
    lv_screen_load(page.create());
    activePage = &page;
    page.onShow();
  }
}

void initPanel() {
  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.setColorDepth(16);
  tft.fillScreen(TFT_BLACK);
}

void initLvgl() {
  lv_init();
  lv_display_t* lvDisplay = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvDisplay, displayFlush);
  lv_display_set_buffers(lvDisplay, drawBuffer, nullptr, sizeof(drawBuffer),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
}

// Configures the radio without connecting. The Bluetooth controller is unused,
// so it is disabled to free its memory.
void configureWiFi() {
  esp_bt_controller_disable();
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConfigPortalTimeout(0);
}

void onWiFiConnected() {
  showPage(contentPage);
  Serial.print("Wi-Fi connected. IP address: ");
  Serial.println(WiFi.localIP());
  if (configPortalStarted) {
    wifiManager.stopConfigPortal();
    configPortalStarted = false;
  }
}

void onWiFiDisconnected() {
  showPage(noWifiPage);
  noWifiPage.showAccessPointInstructions();
  Serial.println("Wi-Fi disconnected.");
  wifiManager.startConfigPortal(WIFI_ACCESS_POINT_NAME);
  configPortalStarted = true;
}

// Reacts to connection changes and, while offline, keeps the setup portal
// and the reconnect attempts going.
void updateWiFiState() {
  const bool isWiFiConnected = WiFi.status() == WL_CONNECTED;
  if (isWiFiConnected && !wasWiFiConnected) {
    onWiFiConnected();
  } else if (!isWiFiConnected && wasWiFiConnected) {
    onWiFiDisconnected();
  }
  wasWiFiConnected = isWiFiConnected;

  if (isWiFiConnected) {
    return;
  }
  if (configPortalStarted) {
    wifiManager.process();
  }
  if (millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL_MS) {
    Serial.println("Retrying Wi-Fi connection...");
    WiFi.reconnect();
    lastWiFiAttempt = millis();
  }
}

// Repaints the screen once an image refresh has finished, so LVGL output that
// was discarded during the refresh gets drawn.
void repaintIfDeferred() {
  if (displayRefreshDeferred && !contentPage.imageRefreshRunning()) {
    displayRefreshDeferred = false;
    lv_obj_invalidate(lv_screen_active());
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Starting ESP32-C3 LVGL Hello World...");

  initPanel();
  initLvgl();
  showPage(noWifiPage);
  noWifiPage.showConnecting();
  lv_timer_handler();
  Serial.println("LVGL UI ready.");

  configureWiFi();
  Serial.println(
      "Connect to the AlertScreen-Setup Wi-Fi network to choose your Wi-Fi.");

  wasWiFiConnected = wifiManager.autoConnect(WIFI_ACCESS_POINT_NAME);
  configPortalStarted = !wasWiFiConnected;
  WiFi.setAutoReconnect(true);
  if (wasWiFiConnected) {
    showPage(contentPage);
    Serial.print("Wi-Fi connected. IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi setup access point is running.");
    noWifiPage.showAccessPointInstructions();
  }

  lastWiFiAttempt = millis();
}

void loop() {
  static uint32_t previousMs = millis();

  const uint32_t now = millis();
  lv_tick_inc(now - previousMs);
  previousMs = now;

  updateWiFiState();
  repaintIfDeferred();

  lv_timer_handler();
  if (activePage != nullptr) {
    activePage->update();
  }
  delay(1);
}
