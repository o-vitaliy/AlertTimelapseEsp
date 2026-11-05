#pragma once

#include <ctime>

#include "Widget.h"

// Time and date in the bottom-left corner. Before an image is shown, the time
// is erased with a black rectangle. Once one is shown, it is restored from a
// cached strip of the image.
class ClockWidget : public Widget {
 public:
  explicit ClockWidget(lgfx::LovyanGFX& panel);

  void onShow() override;
  void onImageDecoded(const uint8_t* jpeg, int size) override;
  void draw() override;
  void tick() override;

 private:
  struct Layout {
    int32_t top;
    int32_t stripWidth;
    int32_t stripHeight;
  };

  static Layout measure(lgfx::LovyanGFX& target, int32_t panelHeight);
  Layout currentLayout() const;
  bool prepareStrip();
  void drawTime(const char* text, const Layout& layout);

  lgfx::LovyanGFX& panel_;
  lgfx::LGFX_Sprite strip_;
  bool stripReady_ = false;
  bool imageReady_ = false;  // The strip holds the pixels of the shown image.
  char timeText_[sizeof("00:00")] = "";
  unsigned long lastTick_ = 0;
};
