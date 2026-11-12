// PortEchoesAnim (port_echoes_anim.h) on a hand-encoded Echoes ANIM: the key stream's bit order,
// 16-bit totals, rotation and offset decoding, frames between keys; the retarget folding Echoes'
// extra joints into Prime's; the baked file's round trip.

#include "port_echoes_anim.h"
#include "port_test.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace PortEchoesAnim;

namespace {

void Put32(std::vector<uint8_t>& out, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(v >> s));
}
void Put16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(uint8_t(v >> 8));
  out.push_back(uint8_t(v));
}
void PutF(std::vector<uint8_t>& out, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  Put32(out, bits);
}

// Bits written from each word's low bit up, words big-endian (CBitLevelLoader's order).
struct BitWriter {
  std::vector<uint32_t> words;
  uint32_t bit = 32;
  void Put(uint32_t value, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
      if (bit == 32) {
        words.push_back(0);
        bit = 0;
      }
      words.back() |= ((value >> i) & 1u) << bit;
      ++bit;
    }
  }
};

bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }

constexpr uint32_t kRotDiv = 0x4000;
constexpr float kOffMul = 0.01f;

// Two channels over 3 frames, keyed at 0 and 2: segment 1 rotates (z from a quarter turn's
// worth to more) and moves; segment 2 only rotates, with its w negative at frame 2.
std::vector<uint8_t> BuildAnim() {
  std::vector<uint8_t> a;
  Put32(a, 2);         // compressed
  Put32(a, 0x200);     // scratch size
  a.push_back(1);
  a.push_back(1);
  PutF(a, 2.f / 30);   // duration
  PutF(a, 1.f / 30);   // interval
  Put32(a, 0);         // root
  Put32(a, 0);         // looping
  Put32(a, kRotDiv);
  PutF(a, kOffMul);
  PutF(a, 0.f);        // scale multiplier
  Put32(a, 2);         // channels
  Put32(a, 0);
  Put32(a, 3);         // frames
  Put32(a, 0b101);     // keyed: 0 and 2
  Put32(a, 2);
  // Segment 1: rotation (x 0/4 bits, y 0/4, z kRotDiv/2 with 6 bits), offset (100, -50, 0; 8 bits).
  a.push_back(1);
  Put16(a, 2);
  Put16(a, 0); a.push_back(4);
  Put16(a, 0); a.push_back(4);
  Put16(a, kRotDiv / 2); a.push_back(6);
  Put16(a, 2);
  Put16(a, 100); a.push_back(8);
  Put16(a, uint16_t(int16_t(-50))); a.push_back(8);
  Put16(a, 0); a.push_back(8);
  Put16(a, 0);         // no scale
  // Segment 2: rotation only, starting at identity.
  a.push_back(2);
  Put16(a, 2);
  Put16(a, 0); a.push_back(5);
  Put16(a, 0); a.push_back(5);
  Put16(a, 0); a.push_back(5);
  Put16(a, 0);
  Put16(a, 0);
  BitWriter w;
  // Frame 2. Segment 1: sign 0, deltas x +3, y -2, z +20; offset +10, -100 (wraps nothing), +1.
  w.Put(0, 1);
  w.Put(3, 4);
  w.Put(uint32_t(-2) & 0xF, 4);
  w.Put(20, 6);
  w.Put(10, 8);
  w.Put(uint32_t(-100) & 0xFF, 8);
  w.Put(1, 8);
  // Segment 2: sign 1 (w negative), x +5.
  w.Put(1, 1);
  w.Put(5, 5);
  w.Put(0, 5);
  w.Put(0, 5);
  for (uint32_t word : w.words) Put32(a, word);
  return a;
}

void TestDecode() {
  const std::vector<uint8_t> bytes = BuildAnim();
  Anim anim;
  std::string error;
  REQUIRE_MSG(Decode(bytes.data(), bytes.size(), anim, &error), error);
  CHECK_EQ(anim.frames, 3u);
  REQUIRE(anim.channels.size() == 2);
  const Channel& c1 = anim.channels[0];
  CHECK(c1.seg == 1 && c1.hasRot && c1.hasOff);
  const float s = 1.5707963f / kRotDiv;
  // Frame 0: z = sin(pi/4).
  CHECK(Near(c1.rot[0].z, std::sin(s * (kRotDiv / 2))));
  CHECK(Near(c1.rot[0].w, std::sqrt(1.f - c1.rot[0].z * c1.rot[0].z)));
  CHECK(Near(c1.off[0].x, 1.f) && Near(c1.off[0].y, -0.5f) && Near(c1.off[0].z, 0.f));
  // Frame 2: the deltas added.
  CHECK(Near(c1.rot[2].x, std::sin(s * 3)));
  CHECK(Near(c1.rot[2].y, std::sin(s * -2)));
  CHECK(Near(c1.rot[2].z, std::sin(s * (kRotDiv / 2 + 20))));
  CHECK(c1.rot[2].w > 0.f);
  CHECK(Near(c1.off[2].x, 1.1f) && Near(c1.off[2].y, -1.5f) && Near(c1.off[2].z, 0.01f));
  // Frame 1 lies between.
  CHECK(Near(c1.off[1].x, 1.05f) && Near(c1.off[1].y, -1.f));
  CHECK(c1.rot[1].z > c1.rot[0].z && c1.rot[1].z < c1.rot[2].z);
  const Channel& c2 = anim.channels[1];
  CHECK(c2.seg == 2 && c2.hasRot && !c2.hasOff);
  CHECK(Near(c2.rot[0].w, 1.f));
  CHECK(c2.rot[2].w < 0.f);
  CHECK(Near(c2.rot[2].x, std::sin(s * 5)));
  // Truncated keys are refused.
  CHECK(!Decode(bytes.data(), bytes.size() - 4, anim, &error));
}

Quat AboutZ(float radians) { return {std::cos(radians / 2), 0.f, 0.f, std::sin(radians / 2)}; }

void TestRetarget() {
  // Echoes: root(0) > Skeleton_Root(1) > Collar(2) > L_shoulder(3).
  const std::vector<Joint> echoes = {
      {0, 97, "root", {}}, {1, 0, "Skeleton_Root", {}}, {2, 1, "Collar", {}}, {3, 2, "L_shoulder", {}}};
  // Prime: root(3) > Skeleton_Root(4) > Pelvis(15), L_shoulder(11) straight under Skeleton_Root.
  const std::vector<Joint> prime = {{3, 0xFFFFFFFF, "root", {0, 0, 0}},
                                    {4, 3, "Skeleton_Root", {0, 0, 1}},
                                    {15, 4, "Pelvis", {0, 0, 0.9f}},
                                    {11, 4, "L_shoulder", {0.2f, 0, 1.5f}}};
  Anim anim;
  anim.frames = 1;
  anim.interval = 1.f / 30;
  Channel root{0, true, true, {AboutZ(0.5f)}, {{0.f, 2.f, 0.f}}};
  Channel collar{2, true, false, {AboutZ(1.5707963f)}, {}};
  Channel shoulder{3, true, false, {AboutZ(1.5707963f)}, {}};
  anim.channels = {root, collar, shoulder};
  Baked baked;
  std::string error;
  REQUIRE_MSG(Retarget(anim, echoes, prime, "spin", baked, &error), error);
  CHECK_EQ(baked.offsetSeg, 4);
  REQUIRE(baked.segs.size() == 3);  // Skeleton_Root, Pelvis, L_shoulder (not the root)
  const auto rotOf = [&](uint8_t seg) {
    for (size_t i = 0; i < baked.segs.size(); ++i) {
      if (baked.segs[i] == seg) return baked.rot[i];
    }
    return Quat{0, 0, 0, 0};
  };
  // Skeleton_Root takes the Echoes root's turn; Pelvis none; the shoulder the Collar's and its own.
  CHECK(Near(rotOf(4).z, AboutZ(0.5f).z));
  CHECK(Near(rotOf(15).w, 1.f));
  CHECK(Near(std::fabs(rotOf(11).z), 1.f, 1e-3f));
  // Skeleton_Root's offset: the root's move plus Prime's bind offset turned by the root.
  const Vec3 turned = Rotate(AboutZ(0.5f), Vec3{0, 0, 1});
  CHECK(Near(baked.off[0].x, turned.x) && Near(baked.off[0].y, 2.f + turned.y) && Near(baked.off[0].z, 1.f));
  // The middle of the body: Skeleton_Root, Pelvis and shoulder positions averaged.
  const Vec3 mid = Middle(baked, prime, 0);
  CHECK(std::isfinite(mid.x) && mid.z > 0.5f);
}

void TestFile() {
  File file;
  file.centre = {0.f, 0.1f, 1.2f};
  Baked c;
  c.name = "B_screw_loop";
  c.interval = 1.f / 30;
  c.frames = 2;
  c.offsetSeg = 4;
  c.segs = {4, 11};
  c.rot = {AboutZ(0.1f), AboutZ(0.2f), AboutZ(0.3f), AboutZ(0.4f)};
  c.off = {{0, 0, 1}, {0, 0, 1.1f}};
  file.clips.push_back(c);
  const std::vector<uint8_t> bytes = Write(file);
  File back;
  REQUIRE(Read(bytes.data(), bytes.size(), back));
  REQUIRE(back.Find("B_screw_loop") != nullptr);
  const Baked& b = *back.Find("B_screw_loop");
  CHECK_EQ(b.frames, 2u);
  CHECK(b.segs == c.segs);
  CHECK(Near(b.rot[3].z, AboutZ(0.4f).z));
  CHECK(Near(b.off[1].z, 1.1f));
  CHECK(Near(back.centre.z, 1.2f));
  CHECK(!Read(bytes.data(), bytes.size() - 1, back));
  // Times: held at the end, or wrapped.
  uint32_t a = 0, n = 0;
  float f = 0.f;
  FrameAt(b, 10.f, false, a, n, f);
  CHECK(a == 1 && n == 1);
  FrameAt(b, 1.5f / 30, true, a, n, f);
  CHECK(a == 0 && n == 1 && Near(f, 0.5f));
}

}  // namespace

int main() {
  TestDecode();
  TestRetarget();
  TestFile();
  return PORT_TEST_RESULT();
}
