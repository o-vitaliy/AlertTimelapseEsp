#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LovyanGFX.hpp>

#include "ContentPage.h"

namespace {

constexpr char IMAGE_URL[] =
    "https://alert-timelapse-833376049856.europe-west1.run.app/image";
constexpr unsigned long IMAGE_REFRESH_INTERVAL_MS = 60000;
constexpr uint32_t IMAGE_TASK_STACK_SIZE = 12288;
constexpr UBaseType_t IMAGE_TASK_PRIORITY = 1;

}  // namespace

ContentPage::ContentPage(lgfx::LGFX_Device& display) : display_(display) {}

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
  imageRefreshPending_ = true;
  imageDisplayed_ = false;
  lv_obj_set_hidden(loadingSpinner_, false);
  lv_label_set_text(loadingLabel_, "Loading...");
  lv_obj_set_hidden(loadingLabel_, false);
}

void ContentPage::update() {
  if (imageTaskCompleted_) {
    imageTaskCompleted_ = false;
    if (imageTaskSucceeded_) {
      imageDisplayed_ = true;
      lv_obj_set_hidden(loadingSpinner_, true);
      lv_obj_set_hidden(loadingLabel_, true);
    } else {
      lv_label_set_text(loadingLabel_, "Image unavailable. Retrying...");
    }
    lv_obj_invalidate(screen_);
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (imageTaskRunning_ ||
      (!imageRefreshPending_ &&
       now - lastImageRefresh_ < IMAGE_REFRESH_INTERVAL_MS)) {
    return;
  }

  lastImageRefresh_ = now;
  imageRefreshPending_ = false;
  imageTaskRunning_ = true;

  if (xTaskCreate(imageRefreshTask, "imageRefresh", IMAGE_TASK_STACK_SIZE,
                  this, IMAGE_TASK_PRIORITY, nullptr) != pdPASS) {
    imageTaskRunning_ = false;
    Serial.println("Image refresh failed: could not start background task.");
  }
}

bool ContentPage::imageRefreshRunning() const {
  return imageTaskRunning_ || imageTaskCompleted_;
}

bool ContentPage::imageDisplayed() const {
  return imageDisplayed_;
}

void ContentPage::imageRefreshTask(void* context) {
  auto* page = static_cast<ContentPage*>(context);
  page->imageTaskSucceeded_ = page->refreshImage();
  page->imageTaskCompleted_ = true;
  page->imageTaskRunning_ = false;
  vTaskDelete(nullptr);
}

bool ContentPage::refreshImage() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, IMAGE_URL)) {
    Serial.println("Image refresh failed: could not initialize HTTP client.");
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(3);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);

  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    Serial.printf("Image refresh failed: HTTP %d.\n", responseCode);
    http.end();
    return false;
  }

  WiFiClient* imageStream = http.getStreamPtr();
  const bool decoded = display_.drawPng(imageStream, 0, 0, 320, 240, 0, 0,
                                        -1.0f, -1.0f);
  http.end();

  if (!decoded) {
    Serial.println("Image refresh failed: PNG decoding failed.");
    return false;
  }

  Serial.println("Content image refreshed.");
  return true;
}
