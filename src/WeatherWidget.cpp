#include "WeatherWidget.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cmath>

#include "SecureHttp.h"

namespace {

constexpr char IP_LOOKUP_URL[] = "https://ipwho.is/";
constexpr char WEATHER_URL_FORMAT[] =
    "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
    "&current=temperature_2m,weather_code";
constexpr unsigned long LOCATION_RETRY_INTERVAL_MS = 600000;
constexpr unsigned long WEATHER_REFRESH_INTERVAL_MS = 600000;
constexpr float WEATHER_TEMP_SCALE = 2.0f;
constexpr float WEATHER_CONDITION_SCALE = 1.5f;
constexpr int32_t WEATHER_PADDING = 4;

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

}  // namespace

WeatherWidget::WeatherWidget(lgfx::LovyanGFX& panel) : panel_(panel) {}

void WeatherWidget::fetch() {
  fetchLocation();
  fetchWeather();
}

void WeatherWidget::fetchLocation() {
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

void WeatherWidget::fetchWeather() {
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

void WeatherWidget::draw() {
  if (!weatherValid_) {
    return;
  }

  char tempText[8];
  snprintf(tempText, sizeof(tempText), "%d", weatherTemperature_);

  panel_.startWrite();
  panel_.setFont(&lgfx::fonts::Font2);
  panel_.setTextDatum(lgfx::textdatum_t::top_left);
  panel_.setTextColor(TFT_WHITE);

  // The degree sign is drawn as a ring, since the font has no glyph for it.
  panel_.setTextSize(WEATHER_TEMP_SCALE);
  const int32_t tempWidth = panel_.textWidth(tempText);
  const int32_t tempHeight = panel_.fontHeight();
  const int32_t unitWidth = panel_.textWidth("C");
  const int32_t ringRadius = tempHeight / 10;
  const int32_t ringSpace = 2 * ringRadius + 4;
  const int32_t tempLineWidth = tempWidth + ringSpace + unitWidth;

  panel_.setTextSize(WEATHER_CONDITION_SCALE);
  const int32_t conditionWidth = panel_.textWidth(weatherCondition_);

  const int32_t right = panel_.width() - WEATHER_PADDING;
  const int32_t top = WEATHER_PADDING;
  const int32_t tempX = right - tempLineWidth;

  panel_.setTextSize(WEATHER_TEMP_SCALE);
  panel_.drawString(tempText, tempX, top);
  panel_.drawCircle(tempX + tempWidth + 2 + ringRadius, top + ringRadius,
                    ringRadius, TFT_WHITE);
  panel_.drawString("C", tempX + tempWidth + ringSpace, top);

  panel_.setTextSize(WEATHER_CONDITION_SCALE);
  panel_.drawString(weatherCondition_, right - conditionWidth, top + tempHeight);
  panel_.endWrite();
}
