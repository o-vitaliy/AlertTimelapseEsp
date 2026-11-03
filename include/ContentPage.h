#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>

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
  lgfx::LGFX_Device& display_;
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

  bool refreshImage();
  static void imageRefreshTask(void* context);
};
