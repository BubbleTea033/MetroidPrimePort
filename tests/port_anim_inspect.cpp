// PortAnimInspect (port_anim_inspect.h) on hand-built bytes: characters and Play animations found
// in an ANCS-shaped buffer by their known ids, and both games' compressed ANIM headers read.

#include "port_anim_inspect.h"
#include "port_test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace PortAnimInspect;

namespace {

void Put32(std::vector<uint8_t>& out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(uint8_t(value >> shift));
}
void PutF(std::vector<uint8_t>& out, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  Put32(out, bits);
}
void PutName(std::vector<uint8_t>& out, const char* name) { out.insert(out.end(), name, name + std::strlen(name) + 1); }

bool KnownIds(uint32_t type, uint32_t id) {
  return (type == kCMDL && id == 0x11111111) || (type == kCSKR && id == 0x22222222) ||
         (type == kCINF && id == 0x33333333) || (type == kANIM && (id == 0xAAAA0001 || id == 0xAAAA0002));
}

void TestFind() {
  std::vector<uint8_t> ancs;
  Put32(ancs, 1);                 // character id
  ancs.push_back(0);
  ancs.push_back(10);             // version
  PutName(ancs, "Samus");
  Put32(ancs, 0x11111111);
  Put32(ancs, 0x22222222);
  Put32(ancs, 0x33333333);
  PutName(ancs, "NotACharacter"); // ids the disc lacks
  Put32(ancs, 0x11111111);
  Put32(ancs, 0x99999999);
  Put32(ancs, 0x33333333);
  Put32(ancs, 2);                 // animation count
  PutName(ancs, "ScrewAttack");   // entry name, then its Play
  Put32(ancs, 0);
  Put32(ancs, 0xAAAA0001);
  Put32(ancs, 7);
  PutName(ancs, "screwattack_spin");
  PutF(ancs, 1.f);
  Put32(ancs, 0);
  Put32(ancs, 4);                 // a nested Play: no entry name before it
  Put32(ancs, 0);
  Put32(ancs, 0xAAAA0002);
  Put32(ancs, 8);
  PutName(ancs, "land");
  Put32(ancs, 0);
  Put32(ancs, 0xBBBB0000);        // unknown ANIM: not a Play
  Put32(ancs, 9);
  PutName(ancs, "ghost");

  const auto characters = FindCharacters(ancs.data(), ancs.size(), KnownIds);
  REQUIRE(characters.size() == 1);
  CHECK(characters[0].name == "Samus");
  CHECK_EQ(characters[0].cinf, 0x33333333u);

  const auto plays = FindPlays(ancs.data(), ancs.size(), KnownIds);
  REQUIRE(plays.size() == 2);
  CHECK(plays[0].entry == "ScrewAttack");
  CHECK(plays[0].primitive == "screwattack_spin");
  CHECK_EQ(plays[0].anim, 0xAAAA0001u);
  CHECK_EQ(plays[0].primitiveIndex, 7u);
  CHECK(plays[1].entry.empty());
  CHECK(plays[1].primitive == "land");

  CHECK(ContainsNoCase(plays[0].entry, "SCREW"));
  CHECK(!ContainsNoCase(plays[1].primitive, "screw"));
  CHECK(Mentions(ancs.data(), ancs.size(), 0x22222222));
  CHECK(!Mentions(ancs.data(), ancs.size(), 0x44444444));
}

std::vector<uint8_t> CompressedHeader(bool echoes) {
  std::vector<uint8_t> anim;
  Put32(anim, 2);       // version
  Put32(anim, 0x100);   // scratch size
  if (echoes) {
    anim.push_back(1);
    anim.push_back(1);
  } else {
    Put32(anim, 0xE0E0E0E0);  // event id
    Put32(anim, 1);
  }
  PutF(anim, 0.5f);     // duration
  PutF(anim, 1.f / 30); // interval
  Put32(anim, 3);       // root
  Put32(anim, 1);       // looping
  Put32(anim, 0x8000);  // rotation divisor
  PutF(anim, 0.01f);    // translation multiplier
  if (echoes) PutF(anim, 0.02f);
  Put32(anim, 42);      // bone channels
  Put32(anim, 0);
  Put32(anim, 16);      // key bitmap bits
  return anim;
}

void TestDescribe() {
  const auto echoes = CompressedHeader(true);
  const std::string e = DescribeAnim(echoes.data(), echoes.size(), true);
  CHECK(ContainsNoCase(e, "duration 0.500"));
  CHECK(ContainsNoCase(e, "root 3"));
  CHECK(ContainsNoCase(e, "42 bone channels"));
  CHECK(ContainsNoCase(e, "scale x0.02"));
  const auto prime = CompressedHeader(false);
  const std::string p = DescribeAnim(prime.data(), prime.size(), false);
  CHECK(ContainsNoCase(p, "42 bone channels"));
  CHECK(ContainsNoCase(p, "event 3772834016"));
  CHECK(ContainsNoCase(DescribeAnim(echoes.data(), 20, true), "too short"));
}

}  // namespace

int main() {
  TestFind();
  TestDescribe();
  return PORT_TEST_RESULT();
}
