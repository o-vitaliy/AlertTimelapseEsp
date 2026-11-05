#pragma once

#include "Widget.h"

// Temperature and condition in the top-right corner. The location comes from an
// IP lookup, and the weather from Open-Meteo, both fetched on the image task.
class WeatherWidget : public Widget {
 public:
  explicit WeatherWidget(lgfx::LovyanGFX& panel);

  void fetch() override;
  void draw() override;

 private:
  void fetchLocation();
  void fetchWeather();

  lgfx::LovyanGFX& panel_;
  bool locationKnown_ = false;
  char latitudeText_[16] = "";
  char longitudeText_[16] = "";
  unsigned long lastLocationAttempt_ = 0;
  unsigned long lastWeatherFetch_ = 0;
  bool weatherValid_ = false;
  int weatherTemperature_ = 0;
  char weatherCondition_[12] = "";
};
