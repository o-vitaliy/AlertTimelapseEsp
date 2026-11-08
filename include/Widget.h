#pragma once

#include <LovyanGFX.hpp>

// Draws part of the map shown on the panel into a 16-bit sprite, as though the
// sprite sat on the panel with its top-left corner at (0, top).
class MapPainter {
 public:
  virtual ~MapPainter() = default;
  virtual void paint(lgfx::LGFX_Sprite& target, int32_t top) const = 0;
};

// An information element drawn over the map. Widgets draw straight to the
// panel or into the map's bands, bypassing LVGL. Each widget is created with
// the panel it is laid out on.
//
// Threads: fetch(), onMapDrawn() and draw() into a band run on the map refresh
// task. The other calls run on the Arduino loop task, and the refresh task
// must not touch LVGL.
class Widget {
 public:
  virtual ~Widget() = default;

  // Called when the content page is shown.
  virtual void onShow() {}

  // Network work, run on the map refresh task after each refresh.
  virtual void fetch() {}

  // Called after a new map has been drawn on the panel. `map` can redraw any
  // part of it for the duration of the call.
  virtual void onMapDrawn(const MapPainter& map) {}

  // Draws the widget, laid out for the panel, onto `target` as though the
  // target sat on the panel with its top-left corner at (0, top). The target is
  // the panel itself (top 0) or one band of the map; drawing is clipped to it.
  virtual void draw(lgfx::LovyanGFX& target, int32_t top) = 0;

  // Called from the loop while the map is shown, for periodic redraws.
  virtual void tick() {}
};
