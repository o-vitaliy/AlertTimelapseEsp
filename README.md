# AlertScreen

ESP32-C3 firmware that shows a live air-raid alert map of Ukraine on a small 320x240 display.

Every minute it reads active alerts from [alerts.in.ua](https://alerts.in.ua) and threat markers from NEPTUN, then draws the map itself. Hromadas are coloured by alert level (red, or yellow for `alert_level: yellow`), and threats appear as icons turned to their heading. A clock and the local weather are shown on top of the map.

No server of its own is needed: the device calls the APIs directly, and the map is compiled into the firmware.

## Hardware

- ESP32-C3 SuperMini, built as `esp32-c3-devkitc-02`
- 2.0" 240x320 ST7789V SPI display, used in landscape
- Optional: an 18650 cell, a TP4056 charger, a 5 V boost converter, a switch and a 3D-printed case

The [assembly manual](docs/ASSEMBLY.md) has the full parts list, the [wiring diagram](docs/wiring.svg), the battery power chain, the case STLs, step-by-step assembly and troubleshooting.

The display's signal pins go to these GPIOs, as set in [`include/DisplayConfig.h`](include/DisplayConfig.h). Power it from `3V3`, not `5V`:

| Display | GPIO |
|---|---|
| CS | 10 |
| DC | 2 |
| RST | 3 |
| SCLK | 4 |
| MOSI (SDA) | 5 |

The board's BOOT button (GPIO 9) is used as a control button; see [Button](#button).

## Building and flashing

You need [PlatformIO](https://platformio.org) and an [alerts.in.ua API token](https://alerts.in.ua/api-request).

The token is compiled into the firmware, and the build fails if it isn't set:

```bash
export ALERTS_API_TOKEN=your-token
```

Then build, flash and open the serial monitor:

```bash
pio run -t upload
```

```bash
pio device monitor
```

The serial monitor runs at 115200 baud over the board's USB port. The 4 MB flash uses the `huge_app.csv` partition scheme, which has no OTA partition, so updates are flashed over USB.

Anyone with physical access to the device can read the token from flash.

## First setup

1. Power the device. It tries the saved Wi-Fi first.
2. If it can't connect, the screen shows setup instructions and the device starts a Wi-Fi network named `AlertScreen-Setup`.
3. Connect your phone to `AlertScreen-Setup`, choose your Wi-Fi network in the page that opens, and enter its password.
4. Once connected, the device shows a loading screen and then the map.

If Wi-Fi drops later, the setup network starts again while the device keeps retrying the saved network every 10 seconds.

## Button

| Action | Result |
|---|---|
| Short press | Restarts the device on release |
| Hold for 5 seconds | Erases the saved Wi-Fi and restarts into setup |

GPIO 9 is a strapping pin. Holding the button while powering on or resetting puts the chip into download mode instead of running the firmware.

## Configuration

Settings are constants at the top of their source files:

| What | Where |
|---|---|
| Refresh interval (60 s) | `IMAGE_REFRESH_INTERVAL_MS` in [`src/ContentPage.cpp`](src/ContentPage.cpp) |
| Alerts and threats URLs | `ALERTS_URL`, `THREATS_URL` in [`src/AlertSources.cpp`](src/AlertSources.cpp) |
| Time zone, NTP server, time and date formats | `CLOCK_*` in [`src/ClockWidget.cpp`](src/ClockWidget.cpp) |
| Weather and location lookup | `WEATHER_URL_FORMAT`, `IP_LOOKUP_URL` in [`src/WeatherWidget.cpp`](src/WeatherWidget.cpp) |
| Display pins, rotation, colour inversion | [`include/DisplayConfig.h`](include/DisplayConfig.h) |

The clock defaults to Kyiv time (`EET-2EEST,M3.5.0/3,M10.5.0/4`). A failed refresh is retried after 10 seconds (`IMAGE_RETRY_INTERVAL_MS`). alerts.in.ua allows only about 8–10 requests a minute per IP, so don't set either interval much lower.

## Data sources and privacy

| Service | Used for | How often |
|---|---|---|
| [alerts.in.ua](https://alerts.in.ua) | Active alerts | Every refresh |
| [NEPTUN](https://neptun.in.ua) | Threat markers | Every refresh |
| [ipwho.is](https://ipwho.is) | Approximate location for the weather | Once per boot, retried every 10 minutes on failure |
| [Open-Meteo](https://open-meteo.com) | Current temperature and conditions | Every 10 minutes |
| `pool.ntp.org` | Time | Synced by the system's SNTP client |

The location lookup sends the device's public IP address to ipwho.is. TLS certificates are not verified.

If the threats request fails, the map is drawn without threats. If the alerts request fails, the whole refresh fails and is retried.

## Threat icons

The icons are PNGs in [`assets/threat_icons/`](assets/threat_icons/). [`tools/threat_icons.py`](tools/threat_icons.py) converts them into `include/ThreatIcons.h`, together with the table that maps NEPTUN threat types to icons. Don't edit the header by hand.

After changing an icon or the table, regenerate the header, then rebuild and reflash. The script needs Pillow:

```bash
python tools/threat_icons.py
```

## The map

The map is stored in [`include/MapData.h`](include/MapData.h) as a pre-rasterized 320x240 grid of hromadas, together with the place-name tables used to match alerts to hromadas. It was generated once, and the generator is no longer in the project, so changing the map size, borders or place names requires writing a new one.

## Development

Built with the Arduino framework, [LVGL 9](https://lvgl.io), [LovyanGFX](https://github.com/lovyan03/LovyanGFX), [WiFiManager](https://github.com/tzapu/WiFiManager) and [ArduinoJson](https://arduinojson.org). There are no automated tests.

[`CLAUDE.md`](CLAUDE.md) describes the architecture in detail, including how LVGL and the map renderer share the display, the background refresh task and the memory constraints.
