#pragma once

#include "MapRenderer.h"

// Downloads the live data the map shows straight from its sources:
// alerts.in.ua (air-raid alerts, needs the ALERTS_API_TOKEN build variable) and
// NEPTUN (threat markers). Both responses are parsed while streaming, one array
// element at a time, so memory use does not grow with the number of alerts.
// Blocking; run them on the map refresh task.

// Clears the state's alert levels and fills them in. Returns false if the
// request or the response failed; the levels are then incomplete.
bool fetchAlerts(map_render::MapState& state);

// Adds the active threats to the state. Returns false if the request or the
// response failed; threats read before the failure are kept.
bool fetchThreats(map_render::MapState& state);
