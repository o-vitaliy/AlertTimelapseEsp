# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for an ESP32-C3 (SuperMini, on the `esp32-c3-devkitc-02` board definition) driving a 240x320 ST7789 SPI display in landscape. Once on Wi-Fi, it downloads a 320x240 JPEG from a Cloud Run endpoint every 60 seconds and shows it. Without Wi-Fi, it shows setup instructions and runs a WiFiManager access point (`AlertScreen-Setup`).

Built with PlatformIO, the Arduino framework, LVGL 9, LovyanGFX and WiFiManager.

## Commands

`pio` may not be on `PATH`; the PlatformIO install is at `~/.platformio/penv/bin/pio`.

```bash
pio run                  # build
pio run -t upload        # build and flash
pio device monitor       # serial monitor (115200, over native USB CDC)
pio run -t clean
```

There is one environment (`esp32-c3-devkitc-02`), so `-e` is not needed. There are no tests or linter configured; `test/` and `lib/` contain only the PlatformIO placeholder READMEs.

## Architecture

### Two renderers share one panel

This is the key non-obvious design. Pixels reach the display in two independent ways:

1. **LVGL** renders widgets and sends them through `displayFlush` in `src/main.cpp`.
2. **`ContentPage`** bypasses LVGL. It downloads the JPEG into a temporary heap buffer, closes the TLS connection to free its memory, then decodes onto the panel with `display_.drawJpg(...)`, so no full framebuffer is held in RAM.

`displayFlush` arbitrates between them by discarding LVGL's output (it still calls `lv_display_flush_ready`) in two cases:

- **While an image refresh is running:** it sets `displayRefreshDeferred`, and `loop()` invalidates the active screen once the refresh ends so that LVGL repaints.
- **While `ContentPage` has an image displayed:** nothing LVGL draws on that page reaches the panel. A new widget on `ContentPage` stays invisible once an image is showing, unless this logic changes too.

The time and date in the bottom-left corner and the weather in the top-right corner of `ContentPage` therefore skip LVGL too: `ContentPage::drawOverlays()` draws them directly on the panel from the main loop, once after each image refresh. The date and weather draw no background box, so the refresh (which repaints the whole screen) is what clears them. The time is the exception: `ContentPage::tickClock()` redraws it every 5 seconds when the minute changes. It first restores the time's strip from `clockStripPixels`, a static buffer that `refreshImage()` fills with the image pixels under the time, then draws the new time. The clock appears once NTP has synced, and the weather once a location and forecast have been fetched.

### Threading

`LV_USE_OS` is `LV_OS_NONE`, so LVGL may only be called from the Arduino `loop()` task. `ContentPage` downloads in a long-lived FreeRTOS task (`imageRefreshTask`) that must not touch LVGL objects. `update()` creates it on first use and wakes it with a task notification for each refresh. It reports back through `volatile` flags (`imageTaskRunning_`, `imageTaskCompleted_`, `imageTaskSucceeded_`), and `ContentPage::update()` applies the UI changes on the main loop.

### Pages

`Page` (`include/Page.h`) is the interface, and `showPage()` in `main.cpp` switches pages only when the active page changes. `create()` lazily builds the LVGL screen and returns the same one on later calls, so screens are never destroyed. `onShow()` runs on each switch; `ContentPage` uses it to reset to the loading state and queue an immediate refresh. `update()` runs every loop iteration.

`NoWifiPage::showConnecting()` and `showAccessPointInstructions()` dereference widgets built in `create()`, so call them only after the page has been created.

### Wi-Fi state machine

`setup()` calls `autoConnect` with a non-blocking config portal. `loop()` then compares `WiFi.status()` to `wasWiFiConnected` to handle transitions:

- **Connected:** show `ContentPage`, stop the portal if it is running, and disable the BT controller.
- **Disconnected:** show `NoWifiPage` and start the portal.
- **While disconnected:** call `wifiManager.process()` every loop and `WiFi.reconnect()` every 10 seconds.

Since the portal is non-blocking, avoid long blocking work on the main loop.

## Configuration

- **Pins and panel:** `include/DisplayConfig.h`. The panel class (`LGFX`) is defined in `main.cpp`.
- **Resolution:** the native size is 240x320 in `DisplayConfig.h`, but the rotated 320x240 is hardcoded separately in `main.cpp` (`SCREEN_WIDTH`, `SCREEN_HEIGHT`) and in `ContentPage.cpp` (the `drawJpg` call and the `IMAGE_URL` query string). Changing rotation or panel size means updating all of them.
- **Image source:** `IMAGE_URL` and `IMAGE_REFRESH_INTERVAL_MS` are at the top of `src/ContentPage.cpp`. TLS certificates are not verified (`client.setInsecure()`).
- **Clock:** `CLOCK_NTP_SERVER`, `CLOCK_TIMEZONE` (a POSIX TZ string), `CLOCK_TIME_FORMAT`, `CLOCK_DATE_FORMAT` and the `CLOCK_TIME_SCALE` / `CLOCK_DATE_SCALE` text sizes and `CLOCK_TICK_INTERVAL_MS` are also at the top of `src/ContentPage.cpp`.
- **Weather:** the location comes from an IP lookup (`IP_LOOKUP_URL`, ipwho.is) that runs once per boot, retrying every 10 minutes after a failure. Weather comes from Open-Meteo (`WEATHER_URL_FORMAT`), refreshed every 10 minutes. Both run inside `imageRefreshTask`, so they add to the image refresh time. The device's public IP is sent to the lookup service.
- **LVGL:** `include/lv_conf.h` is a minimal override, found through `LV_CONF_INCLUDE_SIMPLE` and `-I include`. A Montserrat font size must be enabled there before it can be used; 14, 18 and 24 are currently on. `LV_MEM_SIZE` is reduced to 32 KB because the JPEG buffer must fit in the heap while TLS is connected.
- **Flash:** the `huge_app.csv` partition scheme gives maximum app space and has no OTA partition, so firmware updates must be flashed directly.
