#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>

#include "ClockWidget.h"
#include "Page.h"
#include "WeatherWidget.h"
#include "Widget.h"

class ContentPage : public Page {
 public:
  explicit ContentPage(lgfx::LGFX_Device& display);

  lv_obj_t* create() override;
  void onShow() override;
  void update() override;
  bool imageRefreshRunning() const;
  bool imageDisplayed() const;

 private:
  // Main-loop side of the image refresh. Safe to touch LVGL here.
  void applyImageRefreshResult();
  void startImageRefreshIfDue();
  void drawLoadingClock();
  void drawOverlays();

  // Background-task side. Must not touch LVGL objects.
  static void imageRefreshTask(void* context);
  bool refreshImage();

  lgfx::LGFX_Device& display_;
  ClockWidget clock_;
  WeatherWidget weather_;
  Widget* widgets_[2];

  lv_obj_t* screen_ = nullptr;
  lv_obj_t* loadingSpinner_ = nullptr;
  lv_obj_t* loadingLabel_ = nullptr;

  unsigned long lastImageRefresh_ = 0;
  unsigned long nextImageRefreshDelay_ = 0;
  bool imageRefreshPending_ = true;
  TaskHandle_t imageTask_ = nullptr;
  volatile bool imageTaskRunning_ = false;
  volatile bool imageTaskCompleted_ = false;
  volatile bool imageTaskSucceeded_ = false;
  bool imageDisplayed_ = false;
  bool overlayPending_ = false;
  bool clockPending_ = false;
};
