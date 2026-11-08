#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MapData.h"

// Draws the alert map from the geometry compiled into MapData.h and the alert
// levels and threats collected in a MapState. Plain C++ with no Arduino or
// LovyanGFX dependency, so it can also be built and checked on a computer.
namespace map_render {

constexpr int MAX_THREATS = 200;  // further threats are not drawn

struct Threat {
  float x;  // icon centre in screen pixels
  float y;
  float sinHeading;  // heading turned clockwise from north; 0 and 1 when unknown
  float cosHeading;
  uint8_t icon;
  uint8_t count;  // sources reporting it; the map labels it when above 1
};

class MapState {
 public:
  // Clears every alert level and threat.
  void clear();

  // Sets a hromada's level (0 none, 1 yellow, 2 red) unless it is already higher.
  void raise(uint16_t hromada, uint8_t level);

  // Adds a threat at lat/lon. headingDegrees is clockwise from north, or NAN
  // when unknown. Returns false once MAX_THREATS are stored.
  bool addThreat(float lat, float lon, float headingDegrees, uint8_t icon,
                 uint8_t count);

  int threatCount() const { return threatCount_; }
  const Threat& threat(int index) const { return threats_[index]; }
  uint8_t level(uint16_t hromada) const;

 private:
  uint8_t levels_[(map_data::HROMADA_COUNT + 3) / 4] = {};
  Threat threats_[MAX_THREATS] = {};
  int threatCount_ = 0;
};

// Renders screen rows [top, top + rows), columns [0, width), into `pixels`
// (RGB565, `width` pixels per row). With swapBytes the high byte comes first,
// which is the order LovyanGFX sprites store. Threat icons are included; their
// count labels are not, since they need a font (see labelPosition).
void renderRows(const MapState& state, int top, int rows, int width,
                uint16_t* pixels, bool swapBytes);

// Where a threat's "x N" label starts: left edge and vertical centre.
void labelPosition(const Threat& threat, float& x, float& centreY);

}  // namespace map_render
