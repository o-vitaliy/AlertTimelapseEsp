#pragma once

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

constexpr unsigned long HTTP_TIMEOUT_MS = 15000;

// Prepares an HTTPS request. Certificates are not verified (see CLAUDE.md).
// Returns false if the request could not be started.
bool beginSecureRequest(WiFiClientSecure& client, HTTPClient& http,
                        const char* url);

// GETs a JSON document. Returns false on HTTP or parse failure.
bool fetchJson(const char* url, JsonDocument& doc);
