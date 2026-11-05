#pragma once

#include <LovyanGFX.hpp>

// An information element drawn over the image. Widgets draw straight to the
// panel, bypassing LVGL. Each widget is created with the panel it draws on.
//
// Threads: fetch() and onImageDecoded() run on the image refresh task. The
// other methods run on the Arduino loop task, and fetch() must not touch LVGL.
class Widget {
 public:
  virtual ~Widget() = default;

  // Called when the content page is shown.
  virtual void onShow() {}

  // Network work, run on the image refresh task after each refresh.
  virtual void fetch() {}

  // Called after a new image has been decoded onto the panel. `jpeg` is still
  // valid for the duration of the call.
  virtual void onImageDecoded(const uint8_t* jpeg, int size) {}

  // Draws the widget over the current image. Called once per image refresh.
  virtual void draw() = 0;

  // Called from the loop while the image is shown, for periodic redraws.
  virtual void tick() {}
};
