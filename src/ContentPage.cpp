#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LovyanGFX.hpp>
#include <cmath>
#include <cstring>

#include "ContentPage.h"

namespace {

constexpr char IMAGE_URL[] =
    "https://alert-timelapse-833376049856.europe-west1.run.app/api/snapshot-image"
    "?width=320&height=240&format=jpg&background=000000&legend=false";
constexpr unsigned long IMAGE_REFRESH_INTERVAL_MS = 60000;
constexpr uint32_t IMAGE_TASK_STACK_SIZE = 12288;
constexpr UBaseType_t IMAGE_TASK_PRIORITY = 1;
constexpr int MAX_IMAGE_SIZE = 64 * 1024;
constexpr unsigned long IMAGE_READ_TIMEOUT_MS = 15000;
constexpr char CLOCK_NTP_SERVER[] = "pool.ntp.org";
constexpr char CLOCK_TIMEZONE[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr char CLOCK_TIME_FORMAT[] = "%H:%M";
constexpr char CLOCK_DATE_FORMAT[] = "%d.%m.%Y";
constexpr float CLOCK_TIME_SCALE = 2.5f;
constexpr float CLOCK_DATE_SCALE = 2.0f;
constexpr int32_t CLOCK_PADDING = 4;
constexpr unsigned long CLOCK_TICK_INTERVAL_MS = 5000;
constexpr char IP_LOOKUP_URL[] = "https://ipwho.is/";
constexpr char WEATHER_URL_FORMAT[] =
    "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
    "&current=temperature_2m,weather_code";
constexpr unsigned long LOCATION_RETRY_INTERVAL_MS = 600000;
constexpr unsigned long WEATHER_REFRESH_INTERVAL_MS = 600000;
constexpr float WEATHER_TEMP_SCALE = 2.0f;
constexpr float WEATHER_CONDITION_SCALE = 1.5f;
constexpr int32_t WEATHER_PADDING = 4;

// The clock strip lives in static RAM rather than the heap, so it doesn't split
// the free heap that the image download and TLS need.
constexpr int32_t CLOCK_STRIP_MAX_WIDTH = 96;
constexpr int32_t CLOCK_STRIP_MAX_HEIGHT = 44;
uint16_t clockStripPixels[CLOCK_STRIP_MAX_WIDTH * CLOCK_STRIP_MAX_HEIGHT];

// Maps a WMO weather code (as used by Open-Meteo) to a short word.
const char* weatherConditionText(int code) {
  if (code == 0) return "Clear";
  if (code <= 3) return "Cloudy";
  if (code == 45 || code == 48) return "Fog";
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return "Snow";
  if (code >= 95) return "Storm";
  return "";
}

bool fetchJson(const char* url, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, url)) {
    return false;
  }

  http.setConnectTimeout(15000);
  http.setTimeout(15000);

  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    Serial.printf("Request failed: HTTP %d.\n", responseCode);
    http.end();
    return false;
  }

  // getString() decodes chunked transfer encoding, which the raw stream does not.
  const String body = http.getString();
  http.end();
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.printf("JSON parse failed: %s.\n", error.c_str());
  }
  return !error;
}

}  // namespace

ContentPage::ContentPage(lgfx::LGFX_Device& display)
    : display_(display), clockStrip_(&display) {}

lv_obj_t* ContentPage::create() {
  if (screen_ == nullptr) {
    screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

    loadingSpinner_ = lv_spinner_create(screen_);
    lv_spinner_set_anim_params(loadingSpinner_, 1000, 60);
    lv_obj_set_size(loadingSpinner_, 48, 48);
    lv_obj_align(loadingSpinner_, LV_ALIGN_CENTER, 0, -12);
    lv_obj_set_style_arc_color(loadingSpinner_, lv_color_hex(0x2878F0),
                               LV_PART_INDICATOR);

    loadingLabel_ = lv_label_create(screen_);
    lv_label_set_text(loadingLabel_, "Loading...");
    lv_obj_set_style_text_color(loadingLabel_, lv_color_white(), 0);
    lv_obj_align(loadingLabel_, LV_ALIGN_CENTER, 0, 40);
  }
  return screen_;
}

void ContentPage::onShow() {
  configTzTime(CLOCK_TIMEZONE, CLOCK_NTP_SERVER);
  imageRefreshPending_ = true;
  imageDisplayed_ = false;
  lv_obj_set_hidden(loadingSpinner_, false);
  lv_label_set_text(loadingLabel_, "Loading...");
  lv_obj_set_hidden(loadingLabel_, false);
}

void ContentPage::update() {
  if (imageTaskCompleted_) {
    imageTaskCompleted_ = false;
    overlayPending_ = imageTaskSucceeded_;  // The new image covers the overlays.
    if (imageTaskSucceeded_) {
      imageDisplayed_ = true;
      lv_obj_set_hidden(loadingSpinner_, true);
      lv_obj_set_hidden(loadingLabel_, true);
    } else {
      lv_label_set_text(loadingLabel_, "Image unavailable. Retrying...");
    }
    lv_obj_invalidate(screen_);
  }

  if (imageDisplayed_ && !imageTaskRunning_) {
    drawOverlays();
    tickClock();
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (imageTaskRunning_ ||
      (!imageRefreshPending_ &&
       now - lastImageRefresh_ < IMAGE_REFRESH_INTERVAL_MS)) {
    return;
  }

  lastImageRefresh_ = now;
  imageRefreshPending_ = false;

  if (imageTask_ == nullptr &&
      xTaskCreate(imageRefreshTask, "imageRefresh", IMAGE_TASK_STACK_SIZE,
                  this, IMAGE_TASK_PRIORITY, &imageTask_) != pdPASS) {
    imageTask_ = nullptr;
    Serial.println("Image refresh failed: could not start background task.");
    return;
  }

  imageTaskRunning_ = true;
  xTaskNotifyGive(imageTask_);
}

bool ContentPage::imageRefreshRunning() const {
  return imageTaskRunning_ || imageTaskCompleted_;
}

bool ContentPage::imageDisplayed() const {
  return imageDisplayed_;
}

// Computes where the clock sits in the bottom-left corner. The time strip is
// the band that holds only the time, so it can be restored without touching
// the date below it.
ContentPage::ClockLayout ContentPage::measureClock(lgfx::LovyanGFX& target,
                                                   int32_t panelHeight) {
  target.setFont(&lgfx::fonts::Font2);
  target.setTextSize(CLOCK_TIME_SCALE);
  const int32_t timeWidth = target.textWidth("00:00");
  const int32_t timeHeight = target.fontHeight();
  target.setTextSize(CLOCK_DATE_SCALE);
  const int32_t dateHeight = target.fontHeight();

  ClockLayout layout;
  layout.stripWidth = 2 * CLOCK_PADDING + timeWidth;
  layout.stripHeight = CLOCK_PADDING + timeHeight;
  layout.top = panelHeight - (timeHeight + dateHeight + 2 * CLOCK_PADDING);
  return layout;
}

// Attaches the strip buffer once. It is filled with image pixels at each
// refresh, so the time can be erased without redrawing the whole image.
bool ContentPage::prepareClockStrip() {
  if (clockStripReady_) {
    return true;
  }

  const ClockLayout layout = measureClock(clockStrip_, display_.height());
  if (layout.stripWidth > CLOCK_STRIP_MAX_WIDTH ||
      layout.stripHeight > CLOCK_STRIP_MAX_HEIGHT) {
    Serial.println("Clock strip: layout is larger than the buffer.");
    return false;
  }
  clockStrip_.setBuffer(clockStripPixels, layout.stripWidth,
                        layout.stripHeight, 16);
  clockStripReady_ = true;
  return true;
}

void ContentPage::drawOverlays() {
  struct tm now;
  if (!overlayPending_ || !getLocalTime(&now, 0)) {
    return;
  }

  drawClock(now);
  drawWeather();
  overlayPending_ = false;
}

void ContentPage::drawClockTime(const char* text, const ClockLayout& layout) {
  display_.setFont(&lgfx::fonts::Font2);
  display_.setTextDatum(lgfx::textdatum_t::top_left);
  display_.setTextColor(TFT_WHITE);
  display_.setTextSize(CLOCK_TIME_SCALE);
  display_.drawString(text, CLOCK_PADDING, layout.top + CLOCK_PADDING);
  strncpy(clockTimeText_, text, sizeof(clockTimeText_));
}

void ContentPage::drawClock(const struct tm& now) {
  char timeText[sizeof("00:00")];
  char dateText[sizeof("00.00.0000")];
  strftime(timeText, sizeof(timeText), CLOCK_TIME_FORMAT, &now);
  strftime(dateText, sizeof(dateText), CLOCK_DATE_FORMAT, &now);

  const ClockLayout layout = measureClock(display_, display_.height());
  display_.startWrite();
  drawClockTime(timeText, layout);
  display_.setTextSize(CLOCK_DATE_SCALE);
  display_.drawString(dateText, CLOCK_PADDING,
                      layout.top + layout.stripHeight);
  display_.endWrite();
}

// Redraws only the time, from the cached strip, once the minute has changed.
void ContentPage::tickClock() {
  if (!clockStripReady_ ||
      millis() - lastClockTick_ < CLOCK_TICK_INTERVAL_MS) {
    return;
  }
  lastClockTick_ = millis();

  struct tm now;
  if (!getLocalTime(&now, 0)) {
    return;
  }
  char timeText[sizeof("00:00")];
  strftime(timeText, sizeof(timeText), CLOCK_TIME_FORMAT, &now);
  if (strcmp(timeText, clockTimeText_) == 0) {
    return;
  }

  const ClockLayout layout = measureClock(display_, display_.height());
  clockStrip_.pushSprite(&display_, 0, layout.top);
  display_.startWrite();
  drawClockTime(timeText, layout);
  display_.endWrite();
}

void ContentPage::drawWeather() {
  if (!weatherValid_) {
    return;
  }

  char tempText[8];
  snprintf(tempText, sizeof(tempText), "%d", weatherTemperature_);

  display_.startWrite();
  display_.setFont(&lgfx::fonts::Font2);
  display_.setTextDatum(lgfx::textdatum_t::top_left);
  display_.setTextColor(TFT_WHITE);

  // The degree sign is drawn as a ring, since the font has no glyph for it.
  display_.setTextSize(WEATHER_TEMP_SCALE);
  const int32_t tempWidth = display_.textWidth(tempText);
  const int32_t tempHeight = display_.fontHeight();
  const int32_t unitWidth = display_.textWidth("C");
  const int32_t ringRadius = tempHeight / 10;
  const int32_t ringSpace = 2 * ringRadius + 4;
  const int32_t tempLineWidth = tempWidth + ringSpace + unitWidth;

  display_.setTextSize(WEATHER_CONDITION_SCALE);
  const int32_t conditionWidth = display_.textWidth(weatherCondition_);

  const int32_t right = display_.width() - WEATHER_PADDING;
  const int32_t top = WEATHER_PADDING;
  const int32_t tempX = right - tempLineWidth;

  display_.setTextSize(WEATHER_TEMP_SCALE);
  display_.drawString(tempText, tempX, top);
  display_.drawCircle(tempX + tempWidth + 2 + ringRadius, top + ringRadius,
                      ringRadius, TFT_WHITE);
  display_.drawString("C", tempX + tempWidth + ringSpace, top);

  display_.setTextSize(WEATHER_CONDITION_SCALE);
  display_.drawString(weatherCondition_, right - conditionWidth,
                      top + tempHeight);
  display_.endWrite();
}

void ContentPage::fetchLocation() {
  const unsigned long now = millis();
  if (locationKnown_ ||
      (lastLocationAttempt_ != 0 &&
       now - lastLocationAttempt_ < LOCATION_RETRY_INTERVAL_MS)) {
    return;
  }
  lastLocationAttempt_ = now;

  JsonDocument doc;
  if (!fetchJson(IP_LOOKUP_URL, doc) || !(doc["success"] | false)) {
    Serial.println("Location lookup failed.");
    return;
  }

  JsonVariant latitude = doc["latitude"];
  JsonVariant longitude = doc["longitude"];
  if (latitude.isNull() || longitude.isNull() ||
      serializeJson(latitude, latitudeText_, sizeof(latitudeText_)) == 0 ||
      serializeJson(longitude, longitudeText_, sizeof(longitudeText_)) == 0) {
    Serial.println("Location lookup returned no coordinates.");
    return;
  }

  locationKnown_ = true;
  Serial.printf("Location: %s (%s, %s).\n", doc["city"] | "unknown",
                latitudeText_, longitudeText_);
}

void ContentPage::fetchWeather() {
  const unsigned long now = millis();
  if (!locationKnown_ ||
      (lastWeatherFetch_ != 0 &&
       now - lastWeatherFetch_ < WEATHER_REFRESH_INTERVAL_MS)) {
    return;
  }
  lastWeatherFetch_ = now;

  char url[200];
  snprintf(url, sizeof(url), WEATHER_URL_FORMAT, latitudeText_, longitudeText_);

  JsonDocument doc;
  if (!fetchJson(url, doc)) {
    Serial.println("Weather fetch failed.");
    return;
  }

  JsonObject current = doc["current"];
  if (current.isNull()) {
    Serial.println("Weather response had no current conditions.");
    return;
  }

  weatherTemperature_ =
      static_cast<int>(roundf(current["temperature_2m"] | 0.0f));
  snprintf(weatherCondition_, sizeof(weatherCondition_), "%s",
           weatherConditionText(current["weather_code"] | -1));
  weatherValid_ = true;
  Serial.printf("Weather: %d C, %s.\n", weatherTemperature_, weatherCondition_);
}

void ContentPage::imageRefreshTask(void* context) {
  auto* page = static_cast<ContentPage*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    page->imageTaskSucceeded_ = page->refreshImage();
    page->fetchLocation();
    page->fetchWeather();
    page->imageTaskCompleted_ = true;
    page->imageTaskRunning_ = false;
  }
}

bool ContentPage::refreshImage() {
  Serial.printf("Image refresh: free heap %lu, largest block %lu.\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  prepareClockStrip();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, IMAGE_URL)) {
    Serial.println("Image refresh failed: could not initialize HTTP client.");
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(3);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);

  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    char tlsError[100] = "";
    client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("Image refresh failed: HTTP %d (%s). TLS: %s\n", responseCode,
                  HTTPClient::errorToString(responseCode).c_str(), tlsError);
    http.end();
    return false;
  }

  const int imageSize = http.getSize();
  if (imageSize <= 0 || imageSize > MAX_IMAGE_SIZE) {
    Serial.printf("Image refresh failed: unexpected size %d.\n", imageSize);
    http.end();
    return false;
  }

  auto* image = static_cast<uint8_t*>(malloc(imageSize));
  if (image == nullptr) {
    Serial.printf(
        "Image refresh failed: could not allocate %d bytes, largest block "
        "%lu.\n",
        imageSize, ESP.getMaxAllocHeap());
    http.end();
    return false;
  }

  WiFiClient* imageStream = http.getStreamPtr();
  int received = 0;
  unsigned long lastDataAt = millis();
  while (received < imageSize &&
         millis() - lastDataAt < IMAGE_READ_TIMEOUT_MS) {
    const int available = imageStream->available();
    if (available > 0) {
      const int remaining = imageSize - received;
      const int chunk = imageStream->read(
          image + received, available < remaining ? available : remaining);
      if (chunk > 0) {
        received += chunk;
        lastDataAt = millis();
      }
    } else if (!imageStream->connected()) {
      break;
    } else {
      delay(1);
    }
  }

  // Release the TLS buffers before decoding.
  http.end();
  client.stop();

  if (received != imageSize) {
    Serial.printf("Image refresh failed: received %d of %d bytes.\n", received,
                  imageSize);
    free(image);
    return false;
  }

  const bool decoded = display_.drawJpg(image, imageSize, 0, 0, 320, 240, 0, 0,
                                        -1.0f, -1.0f);
  if (decoded && clockStripReady_) {
    const ClockLayout layout = measureClock(clockStrip_, display_.height());
    clockStrip_.drawJpg(image, imageSize, 0, 0, layout.stripWidth,
                        layout.stripHeight, 0, layout.top, -1.0f, -1.0f);
  }
  free(image);

  if (!decoded) {
    Serial.println("Image refresh failed: JPEG decoding failed.");
    return false;
  }

  Serial.println("Content image refreshed.");
  return true;
}
