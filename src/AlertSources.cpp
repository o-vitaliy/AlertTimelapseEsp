#include "AlertSources.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "PlaceNames.h"
#include "SecureHttp.h"

// platformio.ini passes the ALERTS_API_TOKEN environment variable as a define.
#ifndef ALERTS_API_TOKEN
#error "ALERTS_API_TOKEN is not defined; see build_flags in platformio.ini."
#endif
static_assert(sizeof(ALERTS_API_TOKEN) > 1,
              "Set the ALERTS_API_TOKEN environment variable before building.");

namespace {

constexpr char ALERTS_URL[] = "https://api.alerts.in.ua/v1/alerts/active.json";
constexpr char THREATS_URL[] = "https://neptun.in.ua/api/v1/threats";
constexpr char USER_AGENT[] = "AlertScreen/1.0 (ESP32)";

// Keys that may hold a threat's heading, in the order the server checks them,
// on the threat itself and then inside its "velocity" object.
const char* const HEADING_KEYS[] = {"bearingDeg", "bearing_deg", "bearing",
                                    "headingDeg", "heading_deg", "direction"};

// Next character that is not whitespace, without consuming it; -1 if the
// stream ends or stalls.
int peekNonSpace(Stream& stream) {
  const unsigned long start = millis();
  while (millis() - start < HTTP_TIMEOUT_MS) {
    const int c = stream.peek();
    if (c < 0) {
      delay(1);
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      stream.read();
      continue;
    }
    return c;
  }
  return -1;
}

// Calls handle(object) for each element of the array stored under `key`
// (written with its quotes, e.g. "\"alerts\""), deserializing one element at a
// time through `filter`. The key's first occurrence must be the array.
template <typename Handler>
bool forEachInArray(Stream& stream, const char* key, const JsonDocument& filter,
                    Handler handle) {
  if (!stream.find(key) || !stream.find("[")) {
    Serial.printf("Response has no %s array.\n", key);
    return false;
  }
  if (peekNonSpace(stream) == ']') {
    return true;
  }
  for (;;) {
    JsonDocument element;
    const DeserializationError error = deserializeJson(
        element, stream, DeserializationOption::Filter(filter));
    if (error) {
      Serial.printf("JSON element in %s failed: %s.\n", key, error.c_str());
      return false;
    }
    handle(element.as<JsonObjectConst>());
    const int next = peekNonSpace(stream);
    if (next == ']') return true;
    if (next != ',') {
      Serial.printf("JSON array %s ended unexpectedly.\n", key);
      return false;
    }
    stream.read();
  }
}

// Opens a GET request with a plain (not chunked) body to stream from.
bool startRequest(WiFiClientSecure& client, HTTPClient& http, const char* url,
                  const char* bearerToken) {
  if (!beginSecureRequest(client, http, url)) {
    Serial.printf("Request to %s failed: could not initialize.\n", url);
    return false;
  }
  http.useHTTP10(true);  // HTTP/1.0 responses are never chunked
  http.setUserAgent(USER_AGENT);
  http.addHeader("Accept", "application/json");
  if (bearerToken != nullptr) {
    http.addHeader("Authorization", String("Bearer ") + bearerToken);
  }
  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    Serial.printf("Request to %s failed: HTTP %d (%s).\n", url, responseCode,
                  HTTPClient::errorToString(responseCode).c_str());
    http.end();
    return false;
  }
  return true;
}

bool isNull(JsonVariantConst value) { return value.isNull(); }

// A JSON number, or a string holding one, as the server's float() accepts.
bool readNumber(JsonVariantConst value, double& out) {
  if (value.is<double>()) {
    out = value.as<double>();
    return true;
  }
  if (value.is<const char*>()) {
    const char* text = value.as<const char*>();
    char* end = nullptr;
    out = strtod(text, &end);
    while (end != nullptr && isspace(static_cast<unsigned char>(*end))) ++end;
    return end != text && end != nullptr && *end == '\0';
  }
  return false;
}

bool hasKey(JsonObjectConst object, const char* key) {
  for (JsonPairConst pair : object) {
    if (strcmp(pair.key().c_str(), key) == 0) return true;
  }
  return false;
}

// The value of the first heading key present in `object`, even when it is
// null (the server stops at it too); null if none is present.
JsonVariantConst firstHeadingKey(JsonObjectConst object) {
  for (const char* key : HEADING_KEYS) {
    if (hasKey(object, key)) return object[key];
  }
  return JsonVariantConst();
}

// Mirrors normalize_threats in the server's map_renderer.py.
void addThreat(JsonObjectConst threat, map_render::MapState& state) {
  const char* status = threat["status"] | "";
  if (strcasecmp(status, "active") != 0) return;

  double lat = 0;
  double lon = 0;
  if (!readNumber(threat["lat"], lat) || !readNumber(threat["lon"], lon) ||
      lat < -90 || lat > 90 || lon < -180 || lon > 180) {
    return;
  }

  JsonVariantConst heading = threat["heading"];
  if (isNull(heading)) heading = firstHeadingKey(threat);
  if (isNull(heading) && threat["velocity"].is<JsonObjectConst>()) {
    heading = firstHeadingKey(threat["velocity"].as<JsonObjectConst>());
  }
  double headingDegrees = NAN;
  if (!readNumber(heading, headingDegrees) || !isfinite(headingDegrees)) {
    headingDegrees = NAN;
  }

  JsonVariantConst sources = threat["sourceCount"];
  if (isNull(sources)) sources = threat["source_count"];
  if (isNull(sources)) sources = threat["count"];
  double count = 0;
  if (!readNumber(sources, count) || count < 0) count = 0;

  const char* type = threat["type"] | "";
  state.addThreat(static_cast<float>(lat), static_cast<float>(lon),
                  static_cast<float>(headingDegrees),
                  place_names::threatIcon(*type ? type : "unknown"),
                  static_cast<uint8_t>(count > 255 ? 255 : count));
}

}  // namespace

bool fetchAlerts(map_render::MapState& state) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!startRequest(client, http, ALERTS_URL, ALERTS_API_TOKEN)) {
    return false;
  }

  JsonDocument filter;
  JsonObject fields = filter.to<JsonObject>();
  for (const char* key : {"location_type", "location_oblast", "location_title",
                          "location_raion", "alert_level", "finished_at"}) {
    fields[key] = true;
  }

  state.clear();
  int alerts = 0;
  int unmatched = 0;
  const bool ok = forEachInArray(
      http.getStream(), "\"alerts\"", filter, [&](JsonObjectConst alert) {
        const char* finished = alert["finished_at"] | "";
        if (*finished) return;  // a finished alert
        const char* levelText = alert["alert_level"] | "";
        const uint8_t level = strcasecmp(levelText, "yellow") == 0 ? 1 : 2;
        const place_names::AlertPlace place = {
            alert["location_type"] | "", alert["location_oblast"] | "",
            alert["location_title"] | "", alert["location_raion"] | ""};
        ++alerts;
        if (place_names::applyAlert(place, level, state) == 0) {
          ++unmatched;
          Serial.printf("No hromada found for alert: %s (%s).\n", place.title,
                        place.oblast);
        }
      });
  http.end();
  client.stop();
  if (ok) {
    Serial.printf("Alerts: %d active, %d not matched.\n", alerts, unmatched);
  }
  return ok;
}

bool fetchThreats(map_render::MapState& state) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!startRequest(client, http, THREATS_URL, nullptr)) {
    return false;
  }

  JsonDocument filter;
  JsonObject fields = filter.to<JsonObject>();
  for (const char* key : {"status", "lat", "lon", "heading", "sourceCount",
                          "source_count", "count", "type"}) {
    fields[key] = true;
  }
  JsonObject velocity = fields["velocity"].to<JsonObject>();
  for (const char* key : HEADING_KEYS) {
    fields[key] = true;
    velocity[key] = true;
  }

  const bool ok = forEachInArray(
      http.getStream(), "\"threats\"", filter,
      [&](JsonObjectConst threat) { addThreat(threat, state); });
  http.end();
  client.stop();
  if (ok) {
    Serial.printf("Threats: %d drawn.\n", state.threatCount());
  }
  return ok;
}
