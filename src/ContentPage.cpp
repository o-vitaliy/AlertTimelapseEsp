#include <Arduino.h>
#include <WiFi.h>
#include <LovyanGFX.hpp>

#include "AlertSources.h"
#include "ContentPage.h"

namespace {

constexpr unsigned long IMAGE_REFRESH_INTERVAL_MS = 60000;
constexpr unsigned long IMAGE_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t IMAGE_TASK_STACK_SIZE = 12288;
constexpr UBaseType_t IMAGE_TASK_PRIORITY = 1;

constexpr int32_t MAP_BAND_ROWS = ContentPage::MAP_BAND_ROWS;
// Two band buffers, used in turn: the panel is sent one by DMA, in the
// background, while the next band is rendered into the other.
uint16_t mapBandPixels[2][map_data::WIDTH * MAP_BAND_ROWS];

// While drawing, the refresh task runs above the Arduino loop (priority 1), so
// the loop does not slow the draw down.
constexpr UBaseType_t MAP_DRAW_PRIORITY = 2;

// FNV-1a over a band's pixels, to tell whether it differs from the panel.
uint32_t bandHash(const uint16_t* pixels, size_t count) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < count; ++i) {
    hash = (hash ^ pixels[i]) * 16777619u;
  }
  return hash;
}

bool timeAvailable() {
  struct tm now;
  return getLocalTime(&now, 0);
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
  bandsOnPanelValid_ = false;  // LVGL has drawn the loading screen over them.
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
  // The overlays are part of the map once the time is known; until then they
  // are drawn on the panel once it is.
  overlayPending_ = imageTaskSucceeded_ && !overlaysInMap_;
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
  clock_.draw(display_, 0);
  clockPending_ = false;
}

// Draws every widget over the new image. Waits until the clock has the time
// (NTP sync), so that the overlays appear together.
void ContentPage::drawOverlays() {
  if (!overlayPending_ || !timeAvailable()) {
    return;
  }
  for (Widget* widget : widgets_) {
    widget->draw(display_, 0);
  }
  overlayPending_ = false;
}

void ContentPage::imageRefreshTask(void* context) {
  auto* page = static_cast<ContentPage*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Widgets fetch first, so that the map is drawn with their latest data.
    for (Widget* widget : page->widgets_) {
      widget->fetch();
    }
    page->imageTaskSucceeded_ = page->refreshMap();
    page->imageTaskCompleted_ = true;
    page->imageTaskRunning_ = false;
  }
}

bool ContentPage::refreshMap() {
  Serial.printf("Map refresh: free heap %lu, largest block %lu.\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // mapState_ is only read when drawing, which happens only after a refresh
  // that succeeded, so it can be filled in place.
  if (!fetchAlerts(mapState_)) {
    Serial.println("Map refresh failed: alerts could not be read.");
    return false;
  }
  // The server treats threats as optional too: alerts still show without them.
  if (!fetchThreats(mapState_)) {
    Serial.println("Map refresh: threats could not be read; drawing without them.");
  }

  const unsigned long drawStart = millis();
  vTaskPrioritySet(nullptr, MAP_DRAW_PRIORITY);
  const int bandsPushed = drawMap();
  for (Widget* widget : widgets_) {
    widget->onMapDrawn(*this);
  }
  vTaskPrioritySet(nullptr, IMAGE_TASK_PRIORITY);
  Serial.printf(
      "Map refreshed: %d of %d bands changed, drawn in %lu ms, task stack "
      "left %u bytes.\n",
      bandsPushed, MAP_BAND_COUNT, millis() - drawStart,
      uxTaskGetStackHighWaterMark(nullptr));
  return true;
}

// Renders the map and the overlays band by band, and sends to the panel only
// the bands that differ from what it shows, so an unchanged map is not
// repainted. Returns the number of bands sent.
int ContentPage::drawMap() {
  // Without the time, the clock (and so the overlays) cannot be drawn yet.
  overlaysInMap_ = timeAvailable();
  const bool panelValid = bandsOnPanelValid_;
  lgfx::LGFX_Sprite bands[2] = {lgfx::LGFX_Sprite(&display_),
                                lgfx::LGFX_Sprite(&display_)};
  for (int b = 0; b < 2; ++b) {
    bands[b].setBuffer(mapBandPixels[b], map_data::WIDTH, MAP_BAND_ROWS, 16);
  }
  int pushed = 0;
  int next = 0;  // the buffer to render into; no transfer reads from it
  display_.startWrite();
  for (int i = 0; i < MAP_BAND_COUNT; ++i) {
    const int32_t top = i * MAP_BAND_ROWS;
    lgfx::LGFX_Sprite& band = bands[next];
    paint(band, top);
    if (overlaysInMap_) {
      for (Widget* widget : widgets_) {
        widget->draw(band, top);
      }
    }
    const uint32_t hash =
        bandHash(mapBandPixels[next], map_data::WIDTH * MAP_BAND_ROWS);
    if (!panelValid || hash != bandHashes_[i]) {
      // The previous transfer, from the other buffer, must end first: after
      // this push that buffer is rendered into again.
      display_.waitDMA();
      band.pushSprite(&display_, 0, top);
      bandHashes_[i] = hash;
      ++pushed;
      next ^= 1;
    }
  }
  display_.waitDMA();
  display_.endWrite();
  bandsOnPanelValid_ = true;
  return pushed;
}

void ContentPage::paint(lgfx::LGFX_Sprite& target, int32_t top) const {
  map_render::renderRows(mapState_, top, target.height(), target.width(),
                         static_cast<uint16_t*>(target.getBuffer()), true);
  drawThreatLabels(target, top);
}

// "x N" beside each threat that more than one source reports, in white with a
// black outline. The sprite clips labels that cross its edges.
void ContentPage::drawThreatLabels(lgfx::LGFX_Sprite& target,
                                   int32_t top) const {
  target.setFont(&lgfx::fonts::Font0);
  target.setTextSize(1);
  target.setTextDatum(lgfx::textdatum_t::middle_left);
  const int32_t halfHeight = target.fontHeight() / 2 + 1;
  for (int i = 0; i < mapState_.threatCount(); ++i) {
    const map_render::Threat& threat = mapState_.threat(i);
    if (threat.count <= 1) {
      continue;
    }
    float left = 0;
    float centreY = 0;
    map_render::labelPosition(threat, left, centreY);
    const int32_t x = static_cast<int32_t>(left) + 1;
    const int32_t y = static_cast<int32_t>(centreY) - top;
    if (y + halfHeight < 0 || y - halfHeight >= target.height()) {
      continue;
    }
    char text[sizeof("x 255")];
    snprintf(text, sizeof(text), "x %u", threat.count);
    target.setTextColor(TFT_BLACK);
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx != 0 || dy != 0) {
          target.drawString(text, x + dx, y + dy);
        }
      }
    }
    target.setTextColor(TFT_WHITE);
    target.drawString(text, x, y);
  }
}
