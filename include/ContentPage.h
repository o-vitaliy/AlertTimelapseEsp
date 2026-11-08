#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>

#include "ClockWidget.h"
#include "MapRenderer.h"
#include "Page.h"
#include "WeatherWidget.h"
#include "Widget.h"

// The alert map. The map state is downloaded and drawn on a background task;
// see imageRefreshTask.
class ContentPage : public Page, private MapPainter {
 public:
  // The map is drawn in bands of this many rows, through a static buffer, so
  // no full-screen framebuffer is needed.
  static constexpr int32_t MAP_BAND_ROWS = 8;
  static constexpr int MAP_BAND_COUNT =
      (map_data::HEIGHT + MAP_BAND_ROWS - 1) / MAP_BAND_ROWS;

  explicit ContentPage(lgfx::LGFX_Device& display);

  lv_obj_t* create() override;
  void onShow() override;
  void update() override;
  bool imageRefreshRunning() const;
  bool imageDisplayed() const;

 private:
  // MapPainter. Runs on the refresh task, which owns mapState_.
  void paint(lgfx::LGFX_Sprite& target, int32_t top) const override;

  // Main-loop side of the image refresh. Safe to touch LVGL here.
  void applyImageRefreshResult();
  void startImageRefreshIfDue();
  void drawLoadingClock();
  void drawOverlays();

  // Background-task side. Must not touch LVGL objects.
  static void imageRefreshTask(void* context);
  bool refreshMap();
  int drawMap();
  void drawThreatLabels(lgfx::LGFX_Sprite& target, int32_t top) const;

  lgfx::LGFX_Device& display_;
  map_render::MapState mapState_;
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
  // Hash of each band on the panel; meaningless while bandsOnPanelValid_ is
  // false (nothing drawn yet, or LVGL drew over the map).
  uint32_t bandHashes_[MAP_BAND_COUNT] = {};
  volatile bool bandsOnPanelValid_ = false;
  volatile bool overlaysInMap_ = false;  // The last map drew the overlays.
  bool overlayPending_ = false;
  bool clockPending_ = false;
};
