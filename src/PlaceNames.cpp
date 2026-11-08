#include "PlaceNames.h"

#include "ThreatIcons.h"

#include <ctype.h>
#include <string.h>

namespace place_names {
namespace {

constexpr size_t NAME_SIZE = 160;

// Words map_renderer.norm removes anywhere in a name, in its order. Lower
// case UTF-8.
const char* const GENERIC_WORDS[] = {
    "територіальна громада",
    "територіальна",
    "область",
    "район",
};
constexpr char CITY_PREFIX[] = "м.";  // removed at the start, with spaces after

struct Span {
  uint16_t start = 0;
  uint16_t count = 0;
};

uint16_t readU16(const uint8_t* p) { return p[0] | p[1] << 8; }

uint32_t readU32(const uint8_t* p) {
  return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

uint16_t poolAt(const Span& span, int i) {
  return readU16(&map_data::NAME_POOL[(span.start + i) * 2]);
}

// Byte length of a whitespace character at s, or 0. Covers ASCII whitespace
// and the Unicode spaces likely in names (no-break and narrow spaces).
size_t spaceLength(const unsigned char* s) {
  if (*s == ' ' || (*s >= '\t' && *s <= '\r') || (*s >= 0x1C && *s <= 0x1F)) {
    return 1;
  }
  if (s[0] == 0xC2 && s[1] == 0xA0) return 2;  // U+00A0
  if (s[0] == 0xE2 && s[1] == 0x80 &&
      ((s[2] >= 0x80 && s[2] <= 0x8A) || s[2] == 0xAF || s[2] == 0xA8 ||
       s[2] == 0xA9)) {
    return 3;  // U+2000-200A, U+202F, U+2028, U+2029
  }
  return 0;
}

// Lower-cases ASCII and Ukrainian/Russian Cyrillic, and turns apostrophe
// variants into '.
void lower(const char* text, char* out, size_t size) {
  const auto* s = reinterpret_cast<const unsigned char*>(text);
  size_t n = 0;
  while (*s && n + 4 < size) {
    if (s[0] < 0x80) {
      out[n++] = s[0] == '`' ? '\'' : static_cast<char>(tolower(s[0]));
      s += 1;
    } else if ((s[0] & 0xE0) == 0xC0 && s[1]) {
      unsigned code = (s[0] & 0x1F) << 6 | (s[1] & 0x3F);
      if (code >= 0x400 && code <= 0x40F) {
        code += 0x50;
      } else if (code >= 0x410 && code <= 0x42F) {
        code += 0x20;
      } else if (code == 0x490) {
        code = 0x491;
      }
      if (code == 0x2BC) {  // ʼ
        out[n++] = '\'';
      } else {
        out[n++] = static_cast<char>(0xC0 | code >> 6);
        out[n++] = static_cast<char>(0x80 | (code & 0x3F));
      }
      s += 2;
    } else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
      if (s[0] == 0xE2 && s[1] == 0x80 && (s[2] == 0x98 || s[2] == 0x99)) {
        out[n++] = '\'';  // ‘ ’
      } else {
        memcpy(out + n, s, 3);
        n += 3;
      }
      s += 3;
    } else {
      out[n++] = static_cast<char>(*s++);
    }
  }
  out[n] = '\0';
}

uint32_t hashName(char kind, const char* oblast, const char* name) {
  uint32_t value = 0x811C9DC5u;
  auto mix = [&value](unsigned char byte) { value = (value ^ byte) * 0x01000193u; };
  mix(static_cast<unsigned char>(kind));
  for (const char* p = oblast; *p; ++p) mix(static_cast<unsigned char>(*p));
  if (name != nullptr) {
    mix(0x1F);
    for (const char* p = name; *p; ++p) mix(static_cast<unsigned char>(*p));
  }
  return value;
}

// Finds a name table entry; an empty span when there is none.
Span lookup(char kind, const char* oblast, const char* name) {
  const uint32_t wanted = hashName(kind, oblast, name);
  int low = 0;
  int high = map_data::NAME_KEY_COUNT - 1;
  while (low <= high) {
    const int middle = (low + high) / 2;
    const uint8_t* key = &map_data::NAME_KEYS[middle * 8];
    const uint32_t value = readU32(key);
    if (value == wanted) {
      Span span;
      span.start = readU16(key + 4);
      span.count = readU16(key + 6);
      return span;
    }
    if (value < wanted) {
      low = middle + 1;
    } else {
      high = middle - 1;
    }
  }
  return Span();
}

bool contains(const Span& span, uint16_t hromada) {
  for (int i = 0; i < span.count; ++i) {
    if (poolAt(span, i) == hromada) return true;
  }
  return false;
}

int raiseSpan(const Span& span, uint8_t level, map_render::MapState& state) {
  for (int i = 0; i < span.count; ++i) state.raise(poolAt(span, i), level);
  return span.count;
}

}  // namespace

void normalize(const char* text, char* out, size_t size) {
  char lowered[NAME_SIZE];
  lower(text == nullptr ? "" : text, lowered, sizeof(lowered));

  // Remove the generic words, then collapse and trim spaces.
  char stripped[NAME_SIZE];
  size_t n = 0;
  const char* s = lowered;
  if (strncmp(s, CITY_PREFIX, sizeof(CITY_PREFIX) - 1) == 0) {
    s += sizeof(CITY_PREFIX) - 1;
    while (size_t length = spaceLength(reinterpret_cast<const unsigned char*>(s))) {
      s += length;
    }
  }
  while (*s && n + 1 < sizeof(stripped)) {
    bool removed = false;
    for (const char* word : GENERIC_WORDS) {
      const size_t length = strlen(word);
      if (strncmp(s, word, length) == 0) {
        s += length;
        removed = true;
        break;
      }
    }
    if (!removed) stripped[n++] = *s++;
  }
  stripped[n] = '\0';

  size_t written = 0;
  bool pendingSpace = false;
  for (const char* p = stripped; *p && written + 1 < size;) {
    if (const size_t length = spaceLength(reinterpret_cast<const unsigned char*>(p))) {
      pendingSpace = written > 0;
      p += length;
      continue;
    }
    if (pendingSpace) {
      if (written + 2 >= size) break;
      out[written++] = ' ';
      pendingSpace = false;
    }
    out[written++] = *p++;
  }
  out[written] = '\0';
}

int applyAlert(const AlertPlace& place, uint8_t level, map_render::MapState& state) {
  char oblast[NAME_SIZE];
  char title[NAME_SIZE];
  normalize(place.oblast, oblast, sizeof(oblast));
  normalize(place.title, title, sizeof(title));
  const char* type = place.type == nullptr ? "" : place.type;

  if (strcmp(type, "oblast") == 0) {
    return raiseSpan(lookup('O', title, nullptr), level, state);
  }
  if (strcmp(type, "raion") == 0) {
    return raiseSpan(lookup('R', oblast, title), level, state);
  }
  if (strcmp(type, "hromada") == 0) {
    const Span found = lookup('H', oblast, title);
    char raion[NAME_SIZE];
    normalize(place.raion, raion, sizeof(raion));
    if (found.count > 1 && raion[0] != '\0') {
      // Several hromadas share the name: keep those in the alert's raion.
      const Span inRaion = lookup('R', oblast, raion);
      int kept = 0;
      for (int i = 0; i < found.count; ++i) {
        if (contains(inRaion, poolAt(found, i))) {
          state.raise(poolAt(found, i), level);
          ++kept;
        }
      }
      if (kept > 0) return kept;
    }
    return raiseSpan(found, level, state);
  }
  // A city, or any other type: the hromada centred on the city, then a
  // hromada or oblast with the same name.
  Span span = lookup('C', oblast, title);
  if (span.count == 0) span = lookup('H', oblast, title);
  if (span.count == 0) span = lookup('O', title, nullptr);
  return raiseSpan(span, level, state);
}

uint8_t threatIcon(const char* type) {
  char key[32];
  size_t n = 0;
  for (const char* p = type == nullptr ? "" : type; *p && n + 1 < sizeof(key); ++p) {
    key[n++] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
  }
  key[n] = '\0';
  if (n == 0) return threat_icons::UNKNOWN_ICON;
  if (strstr(key, "group") != nullptr) return threat_icons::GROUP_ICON;
  for (const threat_icons::ThreatIcon& entry : threat_icons::THREAT_ICONS) {
    if (strcmp(key, entry.type) == 0) return entry.icon;
  }
  return threat_icons::UNKNOWN_ICON;
}

}  // namespace place_names
