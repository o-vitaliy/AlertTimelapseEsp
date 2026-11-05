#include "SecureHttp.h"

#include <Arduino.h>

bool beginSecureRequest(WiFiClientSecure& client, HTTPClient& http,
                        const char* url) {
  client.setInsecure();
  if (!http.begin(client, url)) {
    return false;
  }
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  return true;
}

bool fetchJson(const char* url, JsonDocument& doc) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!beginSecureRequest(client, http, url)) {
    return false;
  }

  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    Serial.printf("Request failed: HTTP %d.\n", responseCode);
    http.end();
    return false;
  }

  // getString() decodes chunked transfer encoding, which the raw stream does not.
  const String body = http.getString();
  http.end();
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.printf("JSON parse failed: %s.\n", error.c_str());
  }
  return !error;
}
