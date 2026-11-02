#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_bt.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>

#include "DisplayConfig.h"

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

static constexpr uint16_t SCREEN_WIDTH = 320;
static constexpr uint16_t SCREEN_HEIGHT = 240;
static constexpr uint16_t BUFFER_LINES = 40;
static constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;

static LGFX tft;
static lv_display_t* display = nullptr;
static lv_obj_t* uptimeLabel = nullptr;
static lv_obj_t* messageLabel = nullptr;
static lv_obj_t* demoButton = nullptr;
static uint8_t drawBuffer[SCREEN_WIDTH * BUFFER_LINES * 2];
static unsigned long lastWiFiAttempt = 0;
static bool wasWiFiConnected = false;

static void displayFlush(lv_display_t* display, const lv_area_t* area,
                         uint8_t* pixels) {
  const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.pushImage(area->x1, area->y1, width, height,
                reinterpret_cast<lgfx::rgb565_t*>(pixels));
  tft.endWrite();

  lv_display_flush_ready(display);
}

static void buttonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    lv_label_set_text(messageLabel, "Nice! The button works.");
  }
}

static void updateUptime(lv_timer_t* timer) {
  (void)timer;
  char text[40];
  snprintf(text, sizeof(text), "Uptime: %lu sec",
           static_cast<unsigned long>(millis() / 1000));
  lv_label_set_text(uptimeLabel, text);
}

static void createHelloUi() {
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101827), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "HELLO WORLD");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x55E6A5), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -72);

  messageLabel = lv_label_create(screen);
  lv_label_set_text(messageLabel, "LVGL is running on ESP32-C3");
  lv_obj_set_style_text_font(messageLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(messageLabel, lv_color_hex(0xE6EDF7), 0);
  lv_obj_set_width(messageLabel, 300);
  lv_obj_set_style_text_align(messageLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(messageLabel, LV_ALIGN_CENTER, 0, -32);

  demoButton = lv_button_create(screen);
  lv_obj_set_size(demoButton, 150, 46);
  lv_obj_align(demoButton, LV_ALIGN_CENTER, 0, 25);
  lv_obj_set_style_bg_color(demoButton, lv_color_hex(0x2878F0),
                            LV_PART_MAIN);
  lv_obj_set_style_radius(demoButton, 12, LV_PART_MAIN);
  lv_obj_add_event_cb(demoButton, buttonEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* buttonText = lv_label_create(demoButton);
  lv_label_set_text(buttonText, "PRESS ME");
  lv_obj_center(buttonText);

  uptimeLabel = lv_label_create(screen);
  lv_label_set_text(uptimeLabel, "Uptime: 0 sec");
  lv_obj_set_style_text_color(uptimeLabel, lv_color_hex(0xF7C873), 0);
  lv_obj_set_style_text_font(uptimeLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(uptimeLabel, LV_ALIGN_CENTER, 0, 88);

  lv_timer_create(updateUptime, 1000, nullptr);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Starting ESP32-C3 LVGL Hello World...");

  pinMode(9, INPUT_PULLUP);

  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.setColorDepth(16);
  tft.fillScreen(TFT_BLACK);

  lv_init();
  display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, displayFlush);
  lv_display_set_buffers(display, drawBuffer, nullptr, sizeof(drawBuffer),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  createHelloUi();
  lv_timer_handler();
  Serial.println("LVGL UI ready.");

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  Serial.println(
      "Connect to the AlertScreen-Setup Wi-Fi network to choose your Wi-Fi.");

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
  static uint32_t previousMs = millis();
  static bool lastRawState = HIGH;
  static bool stableState = HIGH;
  static uint32_t debounceStarted = 0;

  const uint32_t now = millis();
  lv_tick_inc(now - previousMs);
  previousMs = now;

  const bool rawState = digitalRead(9);
  if (rawState != lastRawState) {
    lastRawState = rawState;
    debounceStarted = now;
  }
  if ((now - debounceStarted) > 30 && rawState != stableState) {
    stableState = rawState;
    if (stableState == LOW && demoButton != nullptr) {
      lv_obj_send_event(demoButton, LV_EVENT_CLICKED, nullptr);
    }
  }

  lv_timer_handler();

  const bool isWiFiConnected = WiFi.status() == WL_CONNECTED;
  if (isWiFiConnected && !wasWiFiConnected) {
    Serial.print("Wi-Fi connected. IP address: ");
    Serial.println(WiFi.localIP());
  } else if (!isWiFiConnected && wasWiFiConnected) {
    Serial.println("Wi-Fi disconnected.");
  }
  wasWiFiConnected = isWiFiConnected;

  if (!isWiFiConnected &&
      millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL_MS) {
    Serial.println("Retrying Wi-Fi connection...");
    WiFi.reconnect();
    lastWiFiAttempt = millis();
  }

  delay(5);
}
