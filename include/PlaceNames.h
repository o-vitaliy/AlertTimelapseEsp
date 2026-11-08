#pragma once

#include <stddef.h>

#include "MapRenderer.h"

// Matches alerts.in.ua places and NEPTUN threat types to the map, the way the
// AlertTimelapse server does (`Gazetteer`, `norm` and `_threat_icon_source` in
// map_renderer.py), using the tables in MapData.h. Plain C++, like MapRenderer.
namespace place_names {

// One alert, with the alerts.in.ua fields used for matching. Missing fields
// are nullptr.
struct AlertPlace {
  const char* type;    // location_type: oblast, raion, hromada or city
  const char* oblast;  // location_oblast
  const char* title;   // location_title
  const char* raion;   // location_raion
};

// Normalizes a place name as map_renderer.norm does: lower case, one kind of
// apostrophe, generic words removed, spaces collapsed. Writes at most `size`
// bytes including the terminator.
void normalize(const char* text, char* out, size_t size);

// Raises every hromada the alert covers to `level`. Returns how many it
// covers; 0 means the place name was not found.
int applyAlert(const AlertPlace& place, uint8_t level, map_render::MapState& state);

// Icon index for a NEPTUN threat type.
uint8_t threatIcon(const char* type);

}  // namespace place_names
