#define MAP_DATA_IMPLEMENTATION  // MapData.h's arrays are defined here
#include "MapRenderer.h"

#include <math.h>
#include <string.h>

namespace map_render {
namespace {

constexpr uint8_t OUTSIDE = 3;  // colour column for pixels outside the country
constexpr int ICON_SUBSAMPLES = 3;  // per side, for smooth rotated icons
constexpr float DEGREES_TO_RADIANS = 3.14159265f / 180.0f;

uint16_t readU16(const uint8_t* p) { return p[0] | p[1] << 8; }

uint32_t readU32(const uint8_t* p) {
  return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

uint16_t toRgb565(uint32_t r, uint32_t g, uint32_t b) {
  return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
}

// Colour of every border shade over every fill: alert levels 0-2, then outside.
// It depends only on compiled data, so it is built once.
const uint16_t (*colourTable())[4] {
  static uint16_t table[map_data::BORDER_COUNT][4];
  static bool built = false;
  if (!built) {
    for (int shade = 0; shade < map_data::BORDER_COUNT; ++shade) {
      const uint8_t* entry = &map_data::BORDER_PALETTE[shade * 4];
      const uint32_t keep = 255 - entry[3];
      for (int fill = 0; fill < 4; ++fill) {
        const uint32_t rgb = fill == OUTSIDE ? 0 : map_data::LEVEL_RGB[fill];
        const uint32_t channels[3] = {rgb >> 16 & 0xFF, rgb >> 8 & 0xFF,
                                      rgb & 0xFF};
        uint32_t out[3];
        for (int c = 0; c < 3; ++c) {
          out[c] = entry[c] + (channels[c] * keep + 127) / 255;
          if (out[c] > 255) out[c] = 255;
        }
        table[shade][fill] = toRgb565(out[0], out[1], out[2]);
      }
    }
    built = true;
  }
  return table;
}

uint32_t readVarint(const uint8_t*& p) {
  uint32_t value = 0;
  int shift = 0;
  while (*p & 0x80) {
    value |= static_cast<uint32_t>(*p++ & 0x7F) << shift;
    shift += 7;
  }
  return value | static_cast<uint32_t>(*p++) << shift;
}

// Map pixels of one screen row, decoded from the run-length data.
void renderMapRow(const MapState& state, int y, int width, uint16_t* row) {
  const auto table = colourTable();
  const uint8_t* p =
      map_data::ROWS + readU32(&map_data::ROW_OFFSETS[y * 4]);
  int x = 0;
  uint16_t hromada = map_data::NO_HROMADA;
  while (x < map_data::WIDTH && x < width) {
    const uint32_t token = readVarint(p);
    const uint8_t shade = *p++;
    if (!(token & 1)) {
      hromada = readU16(p);
      p += 2;
    }
    const uint8_t fill =
        hromada == map_data::NO_HROMADA ? OUTSIDE : state.level(hromada);
    const uint16_t colour = table[shade][fill];
    int end = x + static_cast<int>(token >> 1) + 1;
    if (end > width) end = width;
    while (x < end) row[x++] = colour;
  }
  while (x < width) row[x++] = 0;
}

// Blends one threat icon, turned to its heading, over the rendered rows.
void drawIcon(const Threat& threat, int top, int rows, int width,
              uint16_t* pixels) {
  const float size = map_data::ICON_SCREEN_SIZE;
  const float reach = size * 0.7072f;  // half the diagonal: any heading fits
  int x0 = static_cast<int>(floorf(threat.x - reach));
  int x1 = static_cast<int>(ceilf(threat.x + reach));
  int y0 = static_cast<int>(floorf(threat.y - reach));
  int y1 = static_cast<int>(ceilf(threat.y + reach));
  if (x0 < 0) x0 = 0;
  if (x1 > width) x1 = width;
  if (y0 < top) y0 = top;
  if (y1 > top + rows) y1 = top + rows;

  if (x0 >= x1 || y0 >= y1) return;

  const uint8_t* icon = &map_data::ICON_RGBA[threat.icon * map_data::ICON_SIZE *
                                             map_data::ICON_SIZE * 4];
  constexpr int samples = ICON_SUBSAMPLES * ICON_SUBSAMPLES;

  // The point on the north-up icon that a screen point shows undoes the
  // clockwise turn, and is linear in the screen position:
  //   u = (dx cos + dy sin) * toSource + half
  //   v = (-dx sin + dy cos) * toSource + half
  // The ESP32-C3 has no FPU, so it is stepped in 16.16 fixed point.
  const float toSource = map_data::ICON_SIZE / size;
  const float half = map_data::ICON_SIZE / 2.0f;
  const float uPerX = threat.cosHeading * toSource;
  const float uPerY = threat.sinHeading * toSource;
  const float vPerX = -threat.sinHeading * toSource;
  const float vPerY = threat.cosHeading * toSource;
  const float firstDx = x0 + 0.5f / ICON_SUBSAMPLES - threat.x;
  const float firstDy = y0 + 0.5f / ICON_SUBSAMPLES - threat.y;
  auto fixed = [](float value) {
    return static_cast<int32_t>(lroundf(value * 65536.0f));
  };
  const int32_t uFirst = fixed(firstDx * uPerX + firstDy * uPerY + half);
  const int32_t vFirst = fixed(firstDx * vPerX + firstDy * vPerY + half);
  const int32_t uStepX = fixed(uPerX), uStepY = fixed(uPerY);
  const int32_t vStepX = fixed(vPerX), vStepY = fixed(vPerY);
  const int32_t uSubX = fixed(uPerX / ICON_SUBSAMPLES);
  const int32_t uSubY = fixed(uPerY / ICON_SUBSAMPLES);
  const int32_t vSubX = fixed(vPerX / ICON_SUBSAMPLES);
  const int32_t vSubY = fixed(vPerY / ICON_SUBSAMPLES);

  for (int py = y0; py < y1; ++py) {
    for (int px = x0; px < x1; ++px) {
      const int32_t uPixel = uFirst + (px - x0) * uStepX + (py - y0) * uStepY;
      const int32_t vPixel = vFirst + (px - x0) * vStepX + (py - y0) * vStepY;
      uint32_t sum[3] = {0, 0, 0};
      uint32_t alphaSum = 0;
      for (int j = 0; j < ICON_SUBSAMPLES; ++j) {
        for (int i = 0; i < ICON_SUBSAMPLES; ++i) {
          // >> on a negative value floors, as the float version did.
          const int sx = (uPixel + i * uSubX + j * uSubY) >> 16;
          const int sy = (vPixel + i * vSubX + j * vSubY) >> 16;
          if (sx < 0 || sy < 0 || sx >= map_data::ICON_SIZE ||
              sy >= map_data::ICON_SIZE) {
            continue;
          }
          const uint8_t* s = &icon[(sy * map_data::ICON_SIZE + sx) * 4];
          for (int c = 0; c < 3; ++c) sum[c] += s[c] * s[3];
          alphaSum += s[3];
        }
      }
      if (alphaSum == 0) continue;

      uint16_t& out = pixels[(py - top) * width + px];
      const uint32_t below[3] = {(out >> 11 & 0x1F) * 255u / 31,
                                 (out >> 5 & 0x3F) * 255u / 63,
                                 (out & 0x1F) * 255u / 31};
      const uint32_t keep = 255 * samples - alphaSum;  // scaled by 255 * samples
      uint32_t mixed[3];
      for (int c = 0; c < 3; ++c) {
        mixed[c] = (sum[c] + below[c] * keep) / (255 * samples);
      }
      out = toRgb565(mixed[0], mixed[1], mixed[2]);
    }
  }
}

}  // namespace

void MapState::clear() {
  memset(levels_, 0, sizeof(levels_));
  threatCount_ = 0;
}

void MapState::raise(uint16_t hromada, uint8_t level) {
  if (hromada >= map_data::HROMADA_COUNT || level > 2 ||
      level <= this->level(hromada)) {
    return;
  }
  const int shift = (hromada & 3) * 2;
  uint8_t& cell = levels_[hromada >> 2];
  cell = static_cast<uint8_t>((cell & ~(3 << shift)) | level << shift);
}

bool MapState::addThreat(float lat, float lon, float headingDegrees,
                         uint8_t icon, uint8_t count) {
  if (threatCount_ >= MAX_THREATS) return false;
  Threat& threat = threats_[threatCount_++];
  threat.x = (lon - map_data::LON_MIN) * map_data::SCALE + map_data::OFFSET_X;
  threat.y = (map_data::LAT_MAX - lat) / map_data::LAT_SCALE * map_data::SCALE +
             map_data::OFFSET_Y;
  const float angle =
      isnan(headingDegrees) ? 0.0f : headingDegrees * DEGREES_TO_RADIANS;
  threat.sinHeading = sinf(angle);
  threat.cosHeading = cosf(angle);
  threat.icon = icon < map_data::ICON_COUNT ? icon : map_data::UNKNOWN_ICON;
  threat.count = count;
  return true;
}

uint8_t MapState::level(uint16_t hromada) const {
  if (hromada >= map_data::HROMADA_COUNT) return 0;
  const uint8_t value = levels_[hromada >> 2] >> ((hromada & 3) * 2) & 3;
  return value < 3 ? value : 0;
}

void renderRows(const MapState& state, int top, int rows, int width,
                uint16_t* pixels, bool swapBytes) {
  for (int r = 0; r < rows; ++r) {
    const int y = top + r;
    uint16_t* row = pixels + r * width;
    if (y < 0 || y >= map_data::HEIGHT) {
      for (int x = 0; x < width; ++x) row[x] = 0;
    } else {
      renderMapRow(state, y, width, row);
    }
  }
  for (int i = 0; i < state.threatCount(); ++i) {
    drawIcon(state.threat(i), top, rows, width, pixels);
  }
  if (swapBytes) {
    for (int i = 0; i < rows * width; ++i) {
      pixels[i] = static_cast<uint16_t>(pixels[i] >> 8 | pixels[i] << 8);
    }
  }
}

void labelPosition(const Threat& threat, float& x, float& centreY) {
  x = threat.x + map_data::ICON_SCREEN_SIZE / 2;
  centreY = threat.y;
}

}  // namespace map_render
