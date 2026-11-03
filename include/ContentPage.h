#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <ctime>

#include "Page.h"

class ContentPage : public Page {
 public:
  explicit ContentPage(lgfx::LGFX_Device& display);

  lv_obj_t* create() override;
  void onShow() override;
  void update() override;
  bool imageRefreshRunning() const;
  bool imageDisplayed() const;

 private:
  struct ClockLayout {
    int32_t top;
    int32_t stripWidth;
    int32_t stripHeight;
  };

  lgfx::LGFX_Device& display_;
  lgfx::LGFX_Sprite clockStrip_;
  bool clockStripReady_ = false;
  char clockTimeText_[sizeof("00:00")] = "";
  unsigned long lastClockTick_ = 0;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* loadingSpinner_ = nullptr;
  lv_obj_t* loadingLabel_ = nullptr;
  unsigned long lastImageRefresh_ = 0;
  bool imageRefreshPending_ = true;
  TaskHandle_t imageTask_ = nullptr;
  volatile bool imageTaskRunning_ = false;
  volatile bool imageTaskCompleted_ = false;
  volatile bool imageTaskSucceeded_ = false;
  bool imageDisplayed_ = false;
  bool overlayPending_ = false;
  bool locationKnown_ = false;
  char latitudeText_[16] = "";
  char longitudeText_[16] = "";
  unsigned long lastLocationAttempt_ = 0;
  unsigned long lastWeatherFetch_ = 0;
  bool weatherValid_ = false;
  int weatherTemperature_ = 0;
  char weatherCondition_[12] = "";

  bool refreshImage();
  void fetchLocation();
  void fetchWeather();
  static ClockLayout measureClock(lgfx::LovyanGFX& target, int32_t panelHeight);
  bool prepareClockStrip();
  void drawOverlays();
  void drawClockTime(const char* text, const ClockLayout& layout);
  void drawClock(const struct tm& now);
  void tickClock();
  void drawWeather();
  static void imageRefreshTask(void* context);
};
