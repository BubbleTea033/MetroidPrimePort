// Echoes animations on Prime's Samus (the Screw Attack's spin, port_screw_body.h): Echoes'
// compressed ANIM decoded frame by frame, its joints retargeted onto Prime's in-game skeleton, and
// the result baked into a small file of the port's own (written by the Echoes import from the
// player's disc, never shipped). Pure: bytes in, numbers out.
//
// The compressed ANIM (Prime 2's CFBStreamedCompression; Prime 1's differs in the header):
//   u32 2 (compressed), u32 scratch size, u8 version, u8 version, f32 duration, f32 interval,
//   u32 root, u32 looping, u32 rotation divisor, f32 offset multiplier, f32 scale multiplier,
//   u32 channel count, u32 unknown, u32 frame count, u32 words[] (a bit per frame: keyed),
//   u32 channels, then each: u8 segment id, three tracks (rotation, offset, scale), each u16 key
//   count and, when not 0, three {i16 initial, u8 bits}; then the keys as one bit stream: 32-bit
//   big-endian words read from their low bit up. Each keyed frame after the first holds, for each
//   channel, the rotation's w sign (1 bit) and its x, y, z deltas, the offset's and the scale's
//   deltas, signed, in the header's widths. The running totals are 16-bit and wrap; a rotation's
//   x, y, z are sin(total * pi/2 / divisor), w the rest with that sign; offsets total * multiplier.
//   A frame not keyed lies between the keyed ones around it (slerp, lerp).
//
// Retargeting: both games build a pose as the parent's rotation times the animation's (the
// skeletons' bind rotations are not used), so each Prime joint takes its Echoes namesake's
// rotation in model space, relative to that of the nearest Prime ancestor with a namesake.
// Echoes' extra joints (Collar, Neck_1, the collars) fold into the Prime joints under them;
// Prime's own extras (Pelvis) stay unrotated. Prime's root (segment 3) is never posed, so the
// Echoes root's turn and move go into Skeleton_Root.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace PortEchoesAnim {

struct Quat {
  float w = 1.f, x = 0.f, y = 0.f, z = 0.f;
};
struct Vec3 {
  float x = 0.f, y = 0.f, z = 0.f;
};

inline Quat Mul(const Quat& a, const Quat& b) {
  return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat Conj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
inline Vec3 Rotate(const Quat& q, const Vec3& v) {
  const Quat r = Mul(Mul(q, Quat{0.f, v.x, v.y, v.z}), Conj(q));
  return {r.x, r.y, r.z};
}
inline Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Quat Normalized(const Quat& q) {
  const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  return n > 0.f ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{};
}
// The shorter way from a to b, t of it (normalized linear: the keys are a frame or few apart).
inline Quat Blend(const Quat& a, const Quat& b, float t) {
  const float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  const float s = d < 0.f ? -t : t;
  return Normalized({a.w * (1.f - t) + b.w * s, a.x * (1.f - t) + b.x * s, a.y * (1.f - t) + b.y * s,
                     a.z * (1.f - t) + b.z * s});
}

inline uint32_t BE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline float BEF(const uint8_t* p) {
  const uint32_t bits = BE32(p);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// A decoded animation: every frame (keyed or not) of every channel.
struct Channel {
  uint8_t seg = 0;
  bool hasRot = false;
  bool hasOff = false;
  std::vector<Quat> rot;  // per frame, when hasRot
  std::vector<Vec3> off;  // per frame, when hasOff
};
struct Anim {
  float duration = 0.f;
  float interval = 0.f;
  uint32_t root = 0;
  uint32_t frames = 0;
  std::vector<Channel> channels;
  const Channel* Find(uint32_t seg) const {
    for (const Channel& c : channels) {
      if (c.seg == seg) return &c;
    }
    return nullptr;
  }
};

namespace detail {
// The key stream: 32-bit big-endian words, each read from its low bit up (CBitLevelLoader).
struct Bits {
  const uint8_t* data;
  size_t words;
  size_t word = 0;
  uint32_t bit = 0;
  bool over = false;
  uint32_t Word(size_t i) const { return i < words ? BE32(data + i * 4) : 0u; }
  uint32_t Unsigned(uint32_t count) {
    uint32_t result = 0;
    for (uint32_t i = 0; i < count; ++i) {
      if (word >= words) {
        over = true;
        return result;
      }
      result |= ((Word(word) >> bit) & 1u) << i;
      if (++bit == 32) {
        bit = 0;
        ++word;
      }
    }
    return result;
  }
  int32_t Signed(uint32_t count) {
    if (count == 0) return 0;
    uint32_t v = Unsigned(count);
    if (count < 32 && (v & (1u << (count - 1)))) v |= ~0u << count;
    return int32_t(v);
  }
};
struct Track {
  bool present = false;
  int16_t initial[3] = {};
  uint8_t bits[3] = {};
};
}  // namespace detail

// Decodes an Echoes compressed ANIM (version 2 with Echoes' header). False with *error on
// anything else or on data that runs short.
inline bool Decode(const uint8_t* data, size_t size, Anim& out, std::string* error) {
  const auto fail = [error](const char* why) {
    if (error) *error = why;
    return false;
  };
  size_t at = 0;
  const auto need = [&](size_t n) { return at <= size && size - at >= n; };
  if (!need(4 + 4 + 2 + 36 + 4) || BE32(data) != 2) return fail("not a compressed ANIM");
  at = 10;  // version, scratch size, the two version bytes
  out = Anim();
  out.duration = BEF(data + at);
  out.interval = BEF(data + at + 4);
  out.root = BE32(data + at + 8);
  const uint32_t rotDiv = BE32(data + at + 16);
  const float offMul = BEF(data + at + 20);
  at += 36;  // ... scale multiplier, channel count, unknown
  const uint32_t frames = BE32(data + at);
  at += 4;
  if (frames == 0 || frames > 100000 || rotDiv == 0) return fail("bad ANIM header");
  const size_t bitmapWords = (frames + 31) / 32;
  if (!need(bitmapWords * 4 + 4)) return fail("ANIM key bitmap runs short");
  std::vector<bool> keyed(frames);
  for (uint32_t f = 0; f < frames; ++f) keyed[f] = (BE32(data + at + (f / 32) * 4) >> (f % 32)) & 1u;
  keyed[0] = true;
  at += bitmapWords * 4;
  const uint32_t count = BE32(data + at);
  at += 4;
  if (count > 256) return fail("bad ANIM channel count");
  std::vector<detail::Track> tracks(count * 3);
  out.channels.resize(count);
  for (uint32_t c = 0; c < count; ++c) {
    if (!need(1)) return fail("ANIM channels run short");
    out.channels[c].seg = data[at++];
    for (int t = 0; t < 3; ++t) {
      if (!need(2)) return fail("ANIM channels run short");
      detail::Track& track = tracks[c * 3 + t];
      track.present = (uint32_t(data[at]) << 8 | data[at + 1]) != 0;
      at += 2;
      if (track.present) {
        if (!need(9)) return fail("ANIM channels run short");
        for (int i = 0; i < 3; ++i) {
          track.initial[i] = int16_t(uint16_t(data[at] << 8 | data[at + 1]));
          track.bits[i] = data[at + 2];
          at += 3;
        }
      }
    }
    out.channels[c].hasRot = tracks[c * 3].present;
    out.channels[c].hasOff = tracks[c * 3 + 1].present;
  }
  detail::Bits bits{data + at, (size - at) / 4};
  out.frames = frames;

  // The running totals per channel: rotation sign and x, y, z; offset x, y, z; scale x, y, z.
  std::vector<int16_t> totals(count * 10);
  for (uint32_t c = 0; c < count; ++c) {
    for (int t = 0; t < 3; ++t) {
      for (int i = 0; i < 3; ++i) totals[c * 10 + 1 + t * 3 + i] = tracks[c * 3 + t].initial[i];
    }
  }
  const float rotScale = 1.5707963f / float(rotDiv);
  const auto quat = [&](uint32_t c) {
    const int16_t* v = &totals[c * 10];
    Quat q;
    q.x = std::sin(rotScale * v[1]);
    q.y = std::sin(rotScale * v[2]);
    q.z = std::sin(rotScale * v[3]);
    q.w = std::sqrt(std::max(0.f, 1.f - (q.x * q.x + q.y * q.y + q.z * q.z)));
    if (v[0] != 0) q.w = -q.w;
    return q;
  };
  const auto vec = [&](uint32_t c) {
    const int16_t* v = &totals[c * 10 + 4];
    return Vec3{v[0] * offMul, v[1] * offMul, v[2] * offMul};
  };
  for (Channel& ch : out.channels) {
    if (ch.hasRot) ch.rot.resize(frames);
    if (ch.hasOff) ch.off.resize(frames);
  }
  const auto store = [&](uint32_t f) {
    for (uint32_t c = 0; c < count; ++c) {
      if (out.channels[c].hasRot) out.channels[c].rot[f] = quat(c);
      if (out.channels[c].hasOff) out.channels[c].off[f] = vec(c);
    }
  };
  store(0);
  uint32_t prev = 0;
  for (uint32_t f = 1; f < frames; ++f) {
    if (!keyed[f]) continue;
    for (uint32_t c = 0; c < count; ++c) {
      int16_t* v = &totals[c * 10];
      const detail::Track* tr = &tracks[c * 3];
      if (tr[0].present) {
        v[0] = int16_t(bits.Unsigned(1));
        for (int i = 0; i < 3; ++i) v[1 + i] = int16_t(v[1 + i] + bits.Signed(tr[0].bits[i]));
      }
      for (int t = 1; t < 3; ++t) {
        if (tr[t].present) {
          for (int i = 0; i < 3; ++i) v[1 + t * 3 + i] = int16_t(v[1 + t * 3 + i] + bits.Signed(tr[t].bits[i]));
        }
      }
    }
    if (bits.over) return fail("ANIM keys run short");
    store(f);
    // The frames between the last key and this one.
    for (uint32_t g = prev + 1; g < f; ++g) {
      const float t = float(g - prev) / float(f - prev);
      for (Channel& ch : out.channels) {
        if (ch.hasRot) ch.rot[g] = Blend(ch.rot[prev], ch.rot[f], t);
        if (ch.hasOff) {
          const Vec3 &a = ch.off[prev], &b = ch.off[f];
          ch.off[g] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }
      }
    }
    prev = f;
  }
  // Frames after the last key hold it.
  for (uint32_t g = prev + 1; g < frames; ++g) {
    for (Channel& ch : out.channels) {
      if (ch.hasRot) ch.rot[g] = ch.rot[prev];
      if (ch.hasOff) ch.off[g] = ch.off[prev];
    }
  }
  return true;
}

// A skeleton as both games' CINFs give it: joints with ids, parents and names; Prime's with their
// bind positions (model space) too.
struct Joint {
  uint32_t id = 0;
  uint32_t parent = 0;
  std::string name;
  Vec3 head;
};

// Prime's Varia shoulder pads are called differently in Echoes.
inline std::string EchoesNameFor(const std::string& prime) {
  if (prime == "L_varias2_SDK") return "L_varia_SDK";
  if (prime == "R_varias2_SDK") return "R_varia_SDK";
  return prime;
}

// A retargeted clip: for each frame, each of segs' rotation, and Skeleton_Root's offset (the
// rest keep the skeleton's).
struct Baked {
  std::string name;
  float interval = 0.f;
  uint32_t frames = 0;
  uint8_t offsetSeg = 0;            // Skeleton_Root
  std::vector<uint8_t> segs;        // the Prime segments posed
  std::vector<Quat> rot;            // frames * segs.size()
  std::vector<Vec3> off;            // frames: offsetSeg's offset from its parent
};

namespace detail {
inline const Joint* ByName(const std::vector<Joint>& joints, const std::string& name) {
  for (const Joint& j : joints) {
    if (j.name == name) return &j;
  }
  return nullptr;
}
inline const Joint* ById(const std::vector<Joint>& joints, uint32_t id) {
  for (const Joint& j : joints) {
    if (j.id == id) return &j;
  }
  return nullptr;
}
}  // namespace detail

// Retargets an Echoes animation (on the Echoes skeleton `echoes`) onto Prime's skeleton `prime`.
inline bool Retarget(const Anim& anim, const std::vector<Joint>& echoes, const std::vector<Joint>& prime,
                     const std::string& name, Baked& out, std::string* error) {
  using detail::ById;
  using detail::ByName;
  const Joint* primeSkel = ByName(prime, "Skeleton_Root");
  const Joint* echoesSkel = ByName(echoes, "Skeleton_Root");
  if (primeSkel == nullptr || echoesSkel == nullptr || anim.frames == 0) {
    if (error) *error = "no Skeleton_Root";
    return false;
  }
  out = Baked();
  out.name = name;
  out.interval = anim.interval;
  out.frames = anim.frames;
  out.offsetSeg = uint8_t(primeSkel->id);
  // Each Prime joint (but the root): its namesake, and that of its nearest ancestor with one.
  // The root (Prime's segment 3, never posed) has none: Skeleton_Root takes the Echoes root's turn.
  struct Map {
    uint8_t seg;
    const Joint* echoes;          // nullptr: unrotated
    const Joint* echoesAncestor;  // nullptr: model space
  };
  std::vector<Map> maps;
  const auto mapped = [&](const Joint& j) -> const Joint* {
    return j.name == "root" ? nullptr : ByName(echoes, EchoesNameFor(j.name));
  };
  for (const Joint& j : prime) {
    if (j.name == "root" || j.id > 255) continue;
    const Joint* e = mapped(j);
    const Joint* ancestor = nullptr;
    for (const Joint* p = ById(prime, j.parent); p != nullptr && ancestor == nullptr; p = ById(prime, p->parent)) {
      ancestor = mapped(*p);
      if (p->parent == p->id) break;
    }
    maps.push_back({uint8_t(j.id), e, ancestor});
    out.segs.push_back(uint8_t(j.id));
  }
  out.rot.resize(size_t(anim.frames) * out.segs.size());
  out.off.resize(anim.frames);
  const Vec3 skelBind = [&] {
    const Joint* parent = ById(prime, primeSkel->parent);
    return parent ? Vec3{primeSkel->head.x - parent->head.x, primeSkel->head.y - parent->head.y,
                         primeSkel->head.z - parent->head.z}
                  : primeSkel->head;
  }();
  std::vector<Quat> model(256);
  std::vector<bool> done(256);
  for (uint32_t f = 0; f < anim.frames; ++f) {
    // Echoes' model-space rotations this frame.
    std::fill(done.begin(), done.end(), false);
    const auto modelRot = [&](const Joint* j, auto&& self) -> Quat {
      if (j == nullptr || j->id > 255) return Quat{};
      if (done[j->id]) return model[j->id];
      const Channel* c = anim.Find(j->id);
      const Quat local = c != nullptr && c->hasRot ? c->rot[f] : Quat{};
      const Joint* parent = j->parent != j->id ? ById(echoes, j->parent) : nullptr;
      model[j->id] = Normalized(Mul(self(parent, self), local));
      done[j->id] = true;
      return model[j->id];
    };
    for (size_t i = 0; i < maps.size(); ++i) {
      const Map& m = maps[i];
      Quat q;
      if (m.echoes != nullptr) {
        q = Normalized(Mul(Conj(modelRot(m.echoesAncestor, modelRot)), modelRot(m.echoes, modelRot)));
      }
      out.rot[size_t(f) * out.segs.size() + i] = q;
    }
    // Skeleton_Root's offset: the Echoes root's move, then its own (its animated one, else Prime's bind),
    // turned by the Echoes root.
    const Joint* echoesRoot = ById(echoes, echoesSkel->parent);
    const Channel* rootCh = echoesRoot ? anim.Find(echoesRoot->id) : nullptr;
    const Channel* skelCh = anim.Find(echoesSkel->id);
    const Quat rootRot = rootCh && rootCh->hasRot ? rootCh->rot[f] : Quat{};
    const Vec3 rootOff = rootCh && rootCh->hasOff ? rootCh->off[f] : Vec3{};
    const Vec3 skelOff = skelCh && skelCh->hasOff ? skelCh->off[f] : skelBind;
    out.off[f] = Add(rootOff, Rotate(rootRot, skelOff));
  }
  return true;
}

// Model-space joint positions of a baked frame on the Prime skeleton (the pose builder's sum:
// a child at its parent plus the parent's rotation of its bind offset), averaged over every
// joint: where the body's middle is.
inline Vec3 Middle(const Baked& clip, const std::vector<Joint>& prime, uint32_t frame) {
  using detail::ById;
  std::vector<Quat> rot(256);
  std::vector<Vec3> pos(256);
  std::vector<bool> done(256);
  const auto localRot = [&](uint32_t id) {
    for (size_t i = 0; i < clip.segs.size(); ++i) {
      if (clip.segs[i] == id) return clip.rot[size_t(frame) * clip.segs.size() + i];
    }
    return Quat{};
  };
  const auto solve = [&](const Joint* j, auto&& self) -> void {
    if (j == nullptr || j->id > 255 || done[j->id]) return;
    const Joint* parent = j->parent != j->id ? ById(prime, j->parent) : nullptr;
    Quat parentRot;
    Vec3 parentPos;
    Vec3 offset = j->head;
    if (parent != nullptr && parent->id <= 255) {
      self(parent, self);
      parentRot = rot[parent->id];
      parentPos = pos[parent->id];
      offset = {j->head.x - parent->head.x, j->head.y - parent->head.y, j->head.z - parent->head.z};
    }
    if (j->id == clip.offsetSeg) offset = clip.off[frame];
    pos[j->id] = Add(parentPos, Rotate(parentRot, offset));
    rot[j->id] = j->name == "root" ? Quat{} : Normalized(Mul(parentRot, localRot(j->id)));
    done[j->id] = true;
  };
  Vec3 sum;
  int n = 0;
  for (const Joint& j : prime) {
    solve(&j, solve);
    if (j.id <= 255 && j.name != "root") {
      sum = Add(sum, pos[j.id]);
      ++n;
    }
  }
  return n > 0 ? Vec3{sum.x / n, sum.y / n, sum.z / n} : Vec3{};
}

// The baked file: "PSA1", u32 clip count, a Vec3 centre (the loop's middle, which the body is
// drawn around), then each clip: u8 name length, name, f32 interval, u32 frames, u8 offset
// segment, u8 segment count, segments, then per frame the offset (3 floats) and each segment's
// rotation (w, x, y, z). Little-endian.
constexpr uint32_t kMagic = 0x31415350;  // "PSA1"
// In the port's user folder (PortPaths::UserFolder), beside the mods folder rather than in it.
inline constexpr const char* kFileName = "echoes_screw_attack.bin";
// The Screw Attack's clips in Echoes' in-game Samus set (its Samus_SinglePlayer character).
inline constexpr const char* kInto = "B_into_screw_loop_samus";
inline constexpr const char* kLoop = "B_screw_loop";
inline constexpr const char* kOut = "B_outof_screw_loop_samus";
inline constexpr const char* kWallBump = "B_outof_screw_loop_wallbump_samus";
inline constexpr const char* kLand = "B_screw_land_samus";

struct File {
  Vec3 centre;
  std::vector<Baked> clips;
  const Baked* Find(const std::string& name) const {
    for (const Baked& c : clips) {
      if (c.name == name) return &c;
    }
    return nullptr;
  }
};

inline std::vector<uint8_t> Write(const File& file) {
  std::vector<uint8_t> out;
  const auto u32 = [&out](uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
  };
  const auto f32 = [&u32](float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    u32(bits);
  };
  u32(kMagic);
  u32(uint32_t(file.clips.size()));
  f32(file.centre.x);
  f32(file.centre.y);
  f32(file.centre.z);
  for (const Baked& c : file.clips) {
    out.push_back(uint8_t(std::min<size_t>(c.name.size(), 255)));
    out.insert(out.end(), c.name.begin(), c.name.begin() + std::min<size_t>(c.name.size(), 255));
    f32(c.interval);
    u32(c.frames);
    out.push_back(c.offsetSeg);
    out.push_back(uint8_t(c.segs.size()));
    out.insert(out.end(), c.segs.begin(), c.segs.end());
    for (uint32_t f = 0; f < c.frames; ++f) {
      f32(c.off[f].x);
      f32(c.off[f].y);
      f32(c.off[f].z);
      for (size_t s = 0; s < c.segs.size(); ++s) {
        const Quat& q = c.rot[size_t(f) * c.segs.size() + s];
        f32(q.w);
        f32(q.x);
        f32(q.y);
        f32(q.z);
      }
    }
  }
  return out;
}

inline bool Read(const uint8_t* data, size_t size, File& out) {
  size_t at = 0;
  const auto need = [&](size_t n) { return at <= size && size - at >= n; };
  const auto u32 = [&]() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(data[at + i]) << (8 * i);
    at += 4;
    return v;
  };
  const auto f32 = [&]() {
    const uint32_t bits = u32();
    float f;
    std::memcpy(&f, &bits, 4);
    return std::isfinite(f) ? f : 0.f;
  };
  out = File();
  if (!need(20) || u32() != kMagic) return false;
  const uint32_t clips = u32();
  out.centre = {f32(), f32(), f32()};
  if (clips > 64) return false;
  for (uint32_t i = 0; i < clips; ++i) {
    Baked c;
    if (!need(1)) return false;
    const size_t length = data[at++];
    if (!need(length + 10)) return false;
    c.name.assign(reinterpret_cast<const char*>(data + at), length);
    at += length;
    c.interval = f32();
    c.frames = u32();
    c.offsetSeg = data[at++];
    const size_t segs = data[at++];
    if (c.frames == 0 || c.frames > 10000 || !need(segs)) return false;
    c.segs.assign(data + at, data + at + segs);
    at += segs;
    if (!need(size_t(c.frames) * (12 + segs * 16))) return false;
    c.rot.resize(size_t(c.frames) * segs);
    c.off.resize(c.frames);
    for (uint32_t f = 0; f < c.frames; ++f) {
      c.off[f] = {f32(), f32(), f32()};
      for (size_t s = 0; s < segs; ++s) {
        Quat q;
        q.w = f32();
        q.x = f32();
        q.y = f32();
        q.z = f32();
        c.rot[size_t(f) * segs + s] = Normalized(q);
      }
    }
    if (!(c.interval > 0.f)) return false;
    out.clips.push_back(std::move(c));
  }
  return true;
}

// A clip's pose at t seconds (held at its last frame past its end, or wrapped when looping):
// the frames either side, and how far between them.
inline void FrameAt(const Baked& clip, float t, bool loop, uint32_t& a, uint32_t& b, float& f) {
  const float frames = float(clip.frames - 1);
  float x = std::max(t, 0.f) / clip.interval;
  if (loop && frames > 0.f) {
    x = std::fmod(x, frames);
  } else {
    x = std::min(x, frames);
  }
  a = uint32_t(x);
  b = std::min(a + 1, clip.frames - 1);
  f = x - float(a);
}

}  // namespace PortEchoesAnim
