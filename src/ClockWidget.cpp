#include "ClockWidget.h"

#include <Arduino.h>
#include <cstring>

namespace {

constexpr char CLOCK_NTP_SERVER[] = "pool.ntp.org";
constexpr char CLOCK_TIMEZONE[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr char CLOCK_TIME_FORMAT[] = "%H:%M";
constexpr char CLOCK_DATE_FORMAT[] = "%d.%m.%Y";
constexpr float CLOCK_TIME_SCALE = 2.5f;
constexpr float CLOCK_DATE_SCALE = 2.0f;
constexpr int32_t CLOCK_PADDING = 4;
constexpr unsigned long CLOCK_TICK_INTERVAL_MS = 5000;

// The strip lives in static RAM rather than the heap, so it doesn't split the
// free heap that the image download and TLS need.
constexpr int32_t CLOCK_STRIP_MAX_WIDTH = 96;
constexpr int32_t CLOCK_STRIP_MAX_HEIGHT = 44;
uint16_t clockStripPixels[CLOCK_STRIP_MAX_WIDTH * CLOCK_STRIP_MAX_HEIGHT];

using ClockText = char[sizeof("00:00")];

void formatClockTime(const struct tm& now, ClockText& out) {
  strftime(out, sizeof(out), CLOCK_TIME_FORMAT, &now);
}

}  // namespace

ClockWidget::ClockWidget(lgfx::LovyanGFX& panel)
    : panel_(panel), strip_(&panel) {}

void ClockWidget::onShow() {
  configTzTime(CLOCK_TIMEZONE, CLOCK_NTP_SERVER);
  imageReady_ = false;  // The page is showing the loading screen again.
}

// Computes where the clock sits in the bottom-left corner. The time strip is
// the band that holds only the time, so it can be restored without touching
// the date below it.
ClockWidget::Layout ClockWidget::measure(lgfx::LovyanGFX& target,
                                         int32_t panelHeight) {
  target.setFont(&lgfx::fonts::Font2);
  target.setTextSize(CLOCK_TIME_SCALE);
  const int32_t timeWidth = target.textWidth("00:00");
  const int32_t timeHeight = target.fontHeight();
  target.setTextSize(CLOCK_DATE_SCALE);
  const int32_t dateHeight = target.fontHeight();

  Layout layout;
  layout.stripWidth = 2 * CLOCK_PADDING + timeWidth;
  layout.stripHeight = CLOCK_PADDING + timeHeight;
  layout.top = panelHeight - (timeHeight + dateHeight + 2 * CLOCK_PADDING);
  return layout;
}

ClockWidget::Layout ClockWidget::currentLayout() const {
  return measure(panel_, panel_.height());
}

// Attaches the strip buffer once. It is attached lazily, on the first decoded
// image, so the panel is configured by then.
bool ClockWidget::prepareStrip() {
  if (stripReady_) {
    return true;
  }

  const Layout layout = measure(strip_, panel_.height());
  if (layout.stripWidth > CLOCK_STRIP_MAX_WIDTH ||
      layout.stripHeight > CLOCK_STRIP_MAX_HEIGHT) {
    Serial.println("Clock strip: layout is larger than the buffer.");
    return false;
  }
  strip_.setBuffer(clockStripPixels, layout.stripWidth, layout.stripHeight, 16);
  stripReady_ = true;
  return true;
}

// Saves the map pixels under the time, so tick() can erase the old time.
void ClockWidget::onMapDrawn(const MapPainter& map) {
  if (!prepareStrip()) {
    return;
  }
  map.paint(strip_, measure(strip_, panel_.height()).top);
  imageReady_ = true;
}

void ClockWidget::drawTime(lgfx::LovyanGFX& target, int32_t top,
                           const char* text, const Layout& layout) {
  target.setFont(&lgfx::fonts::Font2);
  target.setTextDatum(lgfx::textdatum_t::top_left);
  target.setTextColor(TFT_WHITE);
  target.setTextSize(CLOCK_TIME_SCALE);
  target.drawString(text, CLOCK_PADDING, layout.top + CLOCK_PADDING - top);
  strncpy(timeText_, text, sizeof(timeText_));
}

void ClockWidget::draw(lgfx::LovyanGFX& target, int32_t top) {
  struct tm now;
  if (!getLocalTime(&now, 0)) {
    return;
  }
  ClockText timeText;
  char dateText[sizeof("00.00.0000")];
  formatClockTime(now, timeText);
  strftime(dateText, sizeof(dateText), CLOCK_DATE_FORMAT, &now);

  const Layout layout = currentLayout();
  target.startWrite();
  drawTime(target, top, timeText, layout);
  target.setTextSize(CLOCK_DATE_SCALE);
  target.drawString(dateText, CLOCK_PADDING,
                    layout.top + layout.stripHeight - top);
  target.endWrite();
}

// Redraws only the time once the minute has changed. The old time is erased
// from the image strip, or with black while no image is shown.
void ClockWidget::tick() {
  if (millis() - lastTick_ < CLOCK_TICK_INTERVAL_MS) {
    return;
  }
  lastTick_ = millis();

  struct tm now;
  if (!getLocalTime(&now, 0)) {
    return;
  }
  ClockText timeText;
  formatClockTime(now, timeText);
  if (strcmp(timeText, timeText_) == 0) {
    return;
  }

  const Layout layout = currentLayout();
  if (imageReady_) {
    strip_.pushSprite(&panel_, 0, layout.top);
  } else {
    panel_.fillRect(0, layout.top, layout.stripWidth, layout.stripHeight,
                    TFT_BLACK);
  }
  panel_.startWrite();
  drawTime(panel_, 0, timeText, layout);
  panel_.endWrite();
}
