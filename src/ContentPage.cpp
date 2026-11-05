#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LovyanGFX.hpp>

#include "ContentPage.h"
#include "SecureHttp.h"

namespace {

constexpr char IMAGE_URL[] =
    "https://alert-timelapse-833376049856.europe-west1.run.app/api/snapshot-image"
    "?width=320&height=240&format=jpg&background=000000&legend=false";
constexpr int IMAGE_WIDTH = 320;
constexpr int IMAGE_HEIGHT = 240;
constexpr unsigned long IMAGE_REFRESH_INTERVAL_MS = 60000;
constexpr unsigned long IMAGE_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t IMAGE_TASK_STACK_SIZE = 12288;
constexpr UBaseType_t IMAGE_TASK_PRIORITY = 1;
constexpr int MAX_IMAGE_SIZE = 64 * 1024;
constexpr unsigned long IMAGE_READ_TIMEOUT_MS = 15000;

bool timeAvailable() {
  struct tm now;
  return getLocalTime(&now, 0);
}

// Reads up to `size` bytes, giving up after IMAGE_READ_TIMEOUT_MS without new
// data or when the connection closes. Returns the number of bytes read.
int readImageBody(WiFiClient& stream, uint8_t* buffer, int size) {
  int received = 0;
  unsigned long lastDataAt = millis();
  while (received < size && millis() - lastDataAt < IMAGE_READ_TIMEOUT_MS) {
    const int available = stream.available();
    if (available > 0) {
      const int remaining = size - received;
      const int chunk = stream.read(buffer + received,
                                    available < remaining ? available : remaining);
      if (chunk > 0) {
        received += chunk;
        lastDataAt = millis();
      }
    } else if (!stream.connected()) {
      break;
    } else {
      delay(1);
    }
  }
  return received;
}

// Downloads the JPEG into a new heap buffer that the caller must free. Returns
// nullptr on failure.
uint8_t* downloadImage(int& imageSize) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!beginSecureRequest(client, http, IMAGE_URL)) {
    Serial.println("Image refresh failed: could not initialize HTTP client.");
    return nullptr;
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(3);

  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    char tlsError[100] = "";
    client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("Image refresh failed: HTTP %d (%s). TLS: %s\n", responseCode,
                  HTTPClient::errorToString(responseCode).c_str(), tlsError);
    http.end();
    return nullptr;
  }

  imageSize = http.getSize();
  if (imageSize <= 0 || imageSize > MAX_IMAGE_SIZE) {
    Serial.printf("Image refresh failed: unexpected size %d.\n", imageSize);
    http.end();
    return nullptr;
  }

  auto* image = static_cast<uint8_t*>(malloc(imageSize));
  if (image == nullptr) {
    Serial.printf(
        "Image refresh failed: could not allocate %d bytes, largest block "
        "%lu.\n",
        imageSize, ESP.getMaxAllocHeap());
    http.end();
    return nullptr;
  }

  const int received = readImageBody(*http.getStreamPtr(), image, imageSize);

  // Release the TLS buffers before decoding.
  http.end();
  client.stop();

  if (received != imageSize) {
    Serial.printf("Image refresh failed: received %d of %d bytes.\n", received,
                  imageSize);
    free(image);
    return nullptr;
  }
  return image;
}

}  // namespace

ContentPage::ContentPage(lgfx::LGFX_Device& display)
    : display_(display),
      clock_(display),
      weather_(display),
      widgets_{&clock_, &weather_} {}

lv_obj_t* ContentPage::create() {
  if (screen_ == nullptr) {
    screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

    loadingSpinner_ = lv_spinner_create(screen_);
    lv_spinner_set_anim_params(loadingSpinner_, 1000, 60);
    lv_obj_set_size(loadingSpinner_, 48, 48);
    lv_obj_align(loadingSpinner_, LV_ALIGN_CENTER, 0, -12);
    lv_obj_set_style_arc_color(loadingSpinner_, lv_color_hex(0x2878F0),
                               LV_PART_INDICATOR);

    loadingLabel_ = lv_label_create(screen_);
    lv_label_set_text(loadingLabel_, "Loading...");
    lv_obj_set_style_text_color(loadingLabel_, lv_color_white(), 0);
    lv_obj_align(loadingLabel_, LV_ALIGN_CENTER, 0, 40);
  }
  return screen_;
}

void ContentPage::onShow() {
  for (Widget* widget : widgets_) {
    widget->onShow();
  }
  imageRefreshPending_ = true;
  imageDisplayed_ = false;
  clockPending_ = true;
  lv_obj_set_hidden(loadingSpinner_, false);
  lv_label_set_text(loadingLabel_, "Loading...");
  lv_obj_set_hidden(loadingLabel_, false);
}

void ContentPage::update() {
  applyImageRefreshResult();

  // The image task draws straight to the panel, so nothing else may draw while
  // it runs.
  if (!imageTaskRunning_) {
    if (imageDisplayed_) {
      drawOverlays();
    } else {
      drawLoadingClock();
    }
    for (Widget* widget : widgets_) {
      widget->tick();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    startImageRefreshIfDue();
  }
}

bool ContentPage::imageRefreshRunning() const {
  return imageTaskRunning_ || imageTaskCompleted_;
}

bool ContentPage::imageDisplayed() const {
  return imageDisplayed_;
}

void ContentPage::applyImageRefreshResult() {
  if (!imageTaskCompleted_) {
    return;
  }
  imageTaskCompleted_ = false;
  overlayPending_ = imageTaskSucceeded_;  // The new image covers the overlays.
  nextImageRefreshDelay_ = imageTaskSucceeded_ ? IMAGE_REFRESH_INTERVAL_MS
                                               : IMAGE_RETRY_INTERVAL_MS;
  if (imageTaskSucceeded_) {
    imageDisplayed_ = true;
    lv_obj_set_hidden(loadingSpinner_, true);
    lv_obj_set_hidden(loadingLabel_, true);
  }
  // The refresh repainted the screen, which erased the loading clock.
  clockPending_ = true;
  lv_obj_invalidate(screen_);
}

void ContentPage::startImageRefreshIfDue() {
  const unsigned long now = millis();
  if (imageTaskRunning_ ||
      (!imageRefreshPending_ && now - lastImageRefresh_ < nextImageRefreshDelay_)) {
    return;
  }

  lastImageRefresh_ = now;
  imageRefreshPending_ = false;

  if (imageTask_ == nullptr &&
      xTaskCreate(imageRefreshTask, "imageRefresh", IMAGE_TASK_STACK_SIZE,
                  this, IMAGE_TASK_PRIORITY, &imageTask_) != pdPASS) {
    imageTask_ = nullptr;
    Serial.println("Image refresh failed: could not start background task.");
    return;
  }

  imageTaskRunning_ = true;
  xTaskNotifyGive(imageTask_);
}

// Shows the clock on the loading screen, before the image arrives.
void ContentPage::drawLoadingClock() {
  if (!clockPending_ || !timeAvailable()) {
    return;
  }
  clock_.draw();
  clockPending_ = false;
}

// Draws every widget over the new image. Waits until the clock has the time
// (NTP sync), so that the overlays appear together.
void ContentPage::drawOverlays() {
  if (!overlayPending_ || !timeAvailable()) {
    return;
  }
  for (Widget* widget : widgets_) {
    widget->draw();
  }
  overlayPending_ = false;
}

void ContentPage::imageRefreshTask(void* context) {
  auto* page = static_cast<ContentPage*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    page->imageTaskSucceeded_ = page->refreshImage();
    for (Widget* widget : page->widgets_) {
      widget->fetch();
    }
    page->imageTaskCompleted_ = true;
    page->imageTaskRunning_ = false;
  }
}

bool ContentPage::refreshImage() {
  Serial.printf("Image refresh: free heap %lu, largest block %lu.\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  int imageSize = 0;
  uint8_t* image = downloadImage(imageSize);
  if (image == nullptr) {
    return false;
  }

  const bool decoded = display_.drawJpg(image, imageSize, 0, 0, IMAGE_WIDTH,
                                        IMAGE_HEIGHT, 0, 0, -1.0f, -1.0f);
  if (decoded) {
    for (Widget* widget : widgets_) {
      widget->onImageDecoded(image, imageSize);
    }
  }
  free(image);

  if (!decoded) {
    Serial.println("Image refresh failed: JPEG decoding failed.");
    return false;
  }

  Serial.println("Content image refreshed.");
  return true;
}
