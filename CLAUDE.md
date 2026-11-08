# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for an ESP32-C3 (SuperMini, on the `esp32-c3-devkitc-02` board definition) driving a 240x320 ST7789 SPI display in landscape. Once on Wi-Fi, it reads air-raid alerts from the alerts.in.ua API and threat markers from the NEPTUN API every 60 seconds, and draws the alert map of Ukraine itself, from map data compiled into the firmware. Without Wi-Fi, it shows setup instructions and runs a WiFiManager access point (`AlertScreen-Setup`).

Built with PlatformIO, the Arduino framework, LVGL 9, LovyanGFX and WiFiManager.

## Commands

`pio` may not be on `PATH`; the PlatformIO install is at `~/.platformio/penv/bin/pio`.

```bash
export ALERTS_API_TOKEN=...  # alerts.in.ua token, required by every build
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
2. **`ContentPage`** bypasses LVGL. It reads the alerts and threats, closes each TLS connection, then draws the map in 8-row bands through two static sprite buffers (`mapBandPixels`), so no full framebuffer is held in RAM. The panel is sent one buffer by DMA while the next band renders into the other; `waitDMA()` must come before a buffer is pushed again, or half-rendered pixels reach the panel as coloured stripes. It keeps a hash of each band on the panel and sends only the bands that changed, so an unchanged map is not repainted every minute; `onShow()` clears the hashes, since LVGL draws over the map. The draw runs at priority 2, above the Arduino loop.

`displayFlush` arbitrates between them by discarding LVGL's output (it still calls `lv_display_flush_ready`) in two cases:

- **While a map refresh is running:** it sets `displayRefreshDeferred`, and `loop()` invalidates the active screen once the refresh ends so that LVGL repaints.
- **While `ContentPage` has a map displayed:** nothing LVGL draws on that page reaches the panel. A new LVGL widget on `ContentPage` stays invisible once a map is showing, unless this logic changes too.

The overlays on `ContentPage` therefore skip LVGL too. They are `Widget`s (`include/Widget.h`): `ClockWidget` (time and date, bottom left) and `WeatherWidget` (top right). `Widget::draw(target, top)` draws onto the panel or into a band. Once NTP has synced, `drawMap()` paints the overlays into every band, so they are part of each band's hash and are never erased by a refresh. The widgets' `fetch()` runs before the map is drawn, so the band has the latest weather. Before the time is known, `ContentPage::drawOverlays()` draws them on the panel from the main loop instead. Between refreshes, `ClockWidget::tick()` redraws the time every 5 seconds when the minute changes. It first restores the time's strip from `clockStripPixels`, a static buffer that `onMapDrawn()` fills by asking the page (a `MapPainter`) to paint those rows again, then draws the new time. The clock appears once NTP has synced, and the weather once a location and forecast have been fetched.

### Map rendering

The device uses no server of its own: each refresh, `src/AlertSources.cpp` calls the alerts.in.ua API (`/v1/alerts/active.json`, with `ALERTS_API_TOKEN`) and the NEPTUN API (`/v1/threats`) directly. Both responses are parsed while streaming, one array element at a time through an ArduinoJson filter (`forEachInArray`), so memory use does not depend on the number of alerts; the requests use HTTP/1.0 so the body is never chunked. A threats failure is logged and the map is drawn without threats; an alerts failure fails the refresh.

The firmware does not depend on the AlertTimelapse repository, whose server used to do this work; everything it needs is in this repository.

- `src/PlaceNames.cpp` matches each alert's oblast, raion, hromada or city name to hromadas, and maps NEPTUN threat types to icons.
- `fetchThreats` keeps only active threats with valid coordinates, and reads the heading and source count from any of several field names.

The map is never stored as an image. `include/MapData.h` holds:

- the 320x240 map pre-rasterized to a hromada index per pixel, run-length encoded per row, with anti-aliased border shading as a palette index per run;
- the place-name tables (FNV-1a hashes of normalized names to hromada indexes);
- the projection constants and the size icons are drawn at.

It was generated once from the AlertTimelapse geometry, and that generator is no longer kept, so it cannot be regenerated: changing the map size, the borders or the place names means writing a new generator.

The threat icons are in `include/ThreatIcons.h`, generated by `tools/threat_icons.py` from the PNGs in `assets/threat_icons/`: 28x28 RGBA each, numbered by file name, plus the NEPTUN threat type to icon table (`THREAT_TYPES` in the script). Do not edit the header by hand. After changing an icon or the table, run this (it needs Pillow), then rebuild and reflash:

```bash
python tools/threat_icons.py
```

`src/MapRenderer.cpp` collects alert levels and threats in a `MapState` and renders rows (`renderRows`): each pixel's colour is a lookup of its border shade and its hromada's level, then the icons are blended in, turned to their headings. `MapRenderer.cpp` and `PlaceNames.cpp` are plain C++ with no Arduino dependency, so they can be compiled and checked on a computer. The "x N" source-count labels need a font, so `ContentPage::drawThreatLabels()` draws them with LovyanGFX.

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

### Button

`updateButton()` in `main.cpp` polls the BOOT button on GPIO 9 (active low, `INPUT_PULLUP`) every loop. A short press restarts the device on release. Holding it for `BUTTON_LONG_PRESS_MS` (5 s) calls `wifiManager.resetSettings()` and restarts while the button is still held, so the device comes up with the setup portal. GPIO 9 is a strapping pin: holding it during power-on or reset enters the ROM download mode instead of running the firmware.

## Configuration

- **Pins and panel:** `include/DisplayConfig.h`. The panel class (`LGFX`) is defined in `main.cpp`.
- **Resolution:** the native size is 240x320 in `DisplayConfig.h`, but the rotated 320x240 is hardcoded separately in `main.cpp` (`SCREEN_WIDTH`, `SCREEN_HEIGHT`) and in `MapData.h` (`WIDTH`, `HEIGHT`, which the map raster is built for). Changing rotation or panel size means updating all of them, and a new map raster.
- **Map sources:** `ALERTS_URL` and `THREATS_URL` are at the top of `src/AlertSources.cpp`, and `IMAGE_REFRESH_INTERVAL_MS` at the top of `src/ContentPage.cpp`. alerts.in.ua allows about 8-10 requests a minute per IP. TLS certificates are not verified (`client.setInsecure()`).
- **API token:** the alerts.in.ua token comes from the `ALERTS_API_TOKEN` environment variable at build time: `build_flags` in `platformio.ini` turns it into a define, and a `static_assert` in `AlertSources.cpp` fails the build when it is unset or empty. Export it before `pio run`. The token is compiled into the firmware, so anyone with the device can read it from flash.
- **Clock:** `CLOCK_NTP_SERVER`, `CLOCK_TIMEZONE` (a POSIX TZ string), `CLOCK_TIME_FORMAT`, `CLOCK_DATE_FORMAT` and the `CLOCK_TIME_SCALE` / `CLOCK_DATE_SCALE` text sizes and `CLOCK_TICK_INTERVAL_MS` are at the top of `src/ClockWidget.cpp`.
- **Weather:** the location comes from an IP lookup (`IP_LOOKUP_URL`, ipwho.is) that runs once per boot, retrying every 10 minutes after a failure. Weather comes from Open-Meteo (`WEATHER_URL_FORMAT`), refreshed every 10 minutes. Both run inside `imageRefreshTask`, so they add to the map refresh time. The device's public IP is sent to the lookup service.
- **LVGL:** `include/lv_conf.h` is a minimal override, found through `LV_CONF_INCLUDE_SIMPLE` and `-I include`. A Montserrat font size must be enabled there before it can be used; 14, 18 and 24 are currently on. `LV_MEM_SIZE` is reduced to 32 KB to leave heap for TLS.
- **Flash:** the `huge_app.csv` partition scheme gives maximum app space and has no OTA partition, so firmware updates must be flashed directly.
