// Reading animation sets (ANCS) and animations (ANIM) of Metroid Prime and Echoes by their shapes,
// for `--inspect-echoes-anims` (port_echoes_suits_import.cpp): the facts the Screw Attack's
// animation import needs (which animation it is, its format, the two skeletons), gathered without
// a full ANCS parser. The PAS and transition tables between the parts are not read; instead:
//
// - a character is a name followed by its model, skin and skeleton ids, all three ids the disc
//   has (Character: id, version, name, CMDL, CSKR, CINF, ...);
// - an animation is a meta-animation "Play" (type 0) whose ANIM id the disc has, followed by its
//   primitive index and name (MetaAnimation Play: type, ANIM, primitive, name, CharAnimTime);
//   when a name ends just before it, that is the animation set entry's name.
//
// Pure: bytes in, findings out; `known(type, id)` says whether the disc has that resource.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace PortAnimInspect {

constexpr uint32_t kCMDL = 0x434D444C;
constexpr uint32_t kCSKR = 0x43534B52;
constexpr uint32_t kCINF = 0x43494E46;
constexpr uint32_t kANIM = 0x414E494D;

using Known = std::function<bool(uint32_t type, uint32_t id)>;

inline uint32_t U32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline float F32(const uint8_t* p) {
  const uint32_t bits = U32(p);
  float f;
  static_assert(sizeof(f) == sizeof(bits));
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

inline bool Printable(uint8_t c) { return c >= 0x20 && c < 0x7F; }

// The null-terminated name of at least 2 printable characters starting at `at`; empty otherwise.
// `end` gets the offset after its terminator.
inline std::string NameAt(const uint8_t* data, size_t size, size_t at, size_t& end) {
  size_t i = at;
  while (i < size && Printable(data[i])) ++i;
  if (i >= size || data[i] != 0 || i - at < 2) return {};
  end = i + 1;
  return std::string(reinterpret_cast<const char*>(data + at), i - at);
}

// The name ending (with its terminator) just before `at`, if any.
inline std::string NameBefore(const uint8_t* data, size_t at) {
  if (at < 3 || data[at - 1] != 0) return {};
  size_t i = at - 1;
  while (i > 0 && Printable(data[i - 1])) --i;
  if (at - 1 - i < 2) return {};
  return std::string(reinterpret_cast<const char*>(data + i), at - 1 - i);
}

struct Character {
  size_t offset;
  std::string name;
  uint32_t cmdl, cskr, cinf;
};

struct Play {
  size_t offset;
  std::string entry;      // the animation set entry's name, or empty when nested
  std::string primitive;  // the primitive's name
  uint32_t anim;
  uint32_t primitiveIndex;
};

inline std::vector<Character> FindCharacters(const uint8_t* data, size_t size, const Known& known) {
  std::vector<Character> out;
  for (size_t at = 0; at + 2 < size; ++at) {
    if (!Printable(data[at]) || (at > 0 && Printable(data[at - 1]))) continue;
    size_t end = 0;
    const std::string name = NameAt(data, size, at, end);
    if (name.empty() || end + 12 > size) continue;
    const uint32_t cmdl = U32(data + end), cskr = U32(data + end + 4), cinf = U32(data + end + 8);
    if (known(kCMDL, cmdl) && known(kCSKR, cskr) && known(kCINF, cinf)) {
      out.push_back({at, name, cmdl, cskr, cinf});
    }
  }
  return out;
}

inline std::vector<Play> FindPlays(const uint8_t* data, size_t size, const Known& known) {
  std::vector<Play> out;
  for (size_t at = 0; at + 12 < size; ++at) {
    if (U32(data + at) != 0) continue;
    const uint32_t anim = U32(data + at + 4);
    if (!known(kANIM, anim)) continue;
    size_t end = 0;
    const std::string primitive = NameAt(data, size, at + 12, end);
    if (primitive.empty()) continue;
    out.push_back({at, NameBefore(data, at), primitive, anim, U32(data + at + 8)});
  }
  return out;
}

// An ANIM's header as text: the version, and for the compressed format (version 2 in Prime,
// with Echoes' extra scale multiplier) its timing, root, channel and key counts.
inline std::string DescribeAnim(const uint8_t* data, size_t size, bool echoes) {
  char line[256];
  if (size < 4) return "too short";
  const uint32_t version = U32(data);
  if (version == 0) {
    if (size < 4 + 24) return "version 0 (uncompressed), too short";
    std::snprintf(line, sizeof(line), "version 0 (uncompressed): duration %.3f s, interval %.4f s, %u keys, root %u",
                  F32(data + 4), F32(data + 12), U32(data + 20), U32(data + 24));
    return line;
  }
  // Compressed: scratch size, then Prime's event id and 1, or Echoes' u16 0x0101.
  size_t at = 8;
  uint32_t event = 0;
  if (echoes) {
    at += 2;
  } else {
    event = U32(data + at);
    at += 8;
  }
  const size_t need = at + (echoes ? 40 : 36);
  if (size < need) {
    std::snprintf(line, sizeof(line), "version %u, %zu bytes, too short for its header", version, size);
    return line;
  }
  const float duration = F32(data + at), interval = F32(data + at + 4);
  const uint32_t root = U32(data + at + 8), looping = U32(data + at + 12), divisor = U32(data + at + 16);
  const float translation = F32(data + at + 20);
  at += 24;
  float scale = 0.f;
  if (echoes) {
    scale = F32(data + at);
    at += 4;
  }
  const uint32_t channels = U32(data + at), keys = U32(data + at + 8);
  std::snprintf(line, sizeof(line),
                "version %u (compressed), %zu bytes: duration %.3f s, interval %.4f s, root %u, looping %u, "
                "rotation divisor %u, translation x%.5f%s, %u bone channels, %u key bitmap bits%s",
                version, size, duration, interval, root, looping, divisor, translation,
                echoes ? (", scale x" + std::to_string(scale)).c_str() : "", channels, keys,
                echoes ? "" : (", event " + std::to_string(event)).c_str());
  return line;
}

inline bool ContainsNoCase(const std::string& text, const char* needle) {
  for (size_t i = 0; i < text.size(); ++i) {
    size_t j = 0;
    for (; needle[j] != 0 && i + j < text.size(); ++j) {
      char a = text[i + j], b = needle[j];
      if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
      if (a != b) break;
    }
    if (needle[j] == 0) return true;
  }
  return false;
}

// Whether the bytes hold this big-endian u32 anywhere (an ANCS naming a skeleton).
inline bool Mentions(const uint8_t* data, size_t size, uint32_t id) {
  for (size_t at = 0; at + 4 <= size; ++at) {
    if (U32(data + at) == id) return true;
  }
  return false;
}

}  // namespace PortAnimInspect
