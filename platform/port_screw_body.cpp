// The Screw Attack's spinning Samus; see port_screw_body.h.
#include "port_screw_body.h"

#include "port_echoes_anim.h"
#include "port_paths.h"
#include "port_utf8_path.h"

#include "Kyoto/Animation/CCharLayoutInfo.hpp"
#include "Kyoto/Animation/CSegIdList.hpp"
#include "Kyoto/Animation/CSegStatementSet.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "MetroidPrime/CAnimData.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace PortScrewBody {
namespace {

namespace EA = PortEchoesAnim;

struct State {
  bool loaded = false;
  bool valid = false;
  std::string status = "not loaded";
  EA::File file;
  const EA::Baked* into = nullptr;
  const EA::Baked* loop = nullptr;
  // The attack.
  const CAnimData* target = nullptr;
  bool showing = false;
  bool inLoop = false;
  float time = 0.f;
};
State s;

void Load() {
  if (s.loaded) {
    return;
  }
  s.loaded = true;
  s.valid = false;
  s.into = s.loop = nullptr;
  const std::string path = PortPaths::UserFolder() + EA::kFileName;
  std::ifstream in(PortUtf8::FromUtf8(path), std::ios::binary);
  if (!in) {
    s.status = "not imported (import the Echoes suits, or run --import-echoes-screw)";
    return;
  }
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!EA::Read(data.data(), data.size(), s.file) || s.file.Find(EA::kLoop) == nullptr) {
    s.status = "unreadable; import it again";
    return;
  }
  s.into = s.file.Find(EA::kInto);
  s.loop = s.file.Find(EA::kLoop);
  s.valid = true;
  char line[96];
  std::snprintf(line, sizeof(line), "%zu clips from Echoes", s.file.clips.size());
  s.status = line;
}

float Duration(const EA::Baked& clip) { return clip.interval * float(clip.frames - 1); }

} // namespace

bool Available() {
  Load();
  return s.valid;
}

std::string Status() {
  Load();
  return s.status;
}

void Reload() {
  s.loaded = false;
  s.showing = false;
}

void Begin(CAnimData* playerAnim) {
  if (!Available() || playerAnim == nullptr) {
    return;
  }
  s.target = playerAnim;
  s.showing = true;
  s.inLoop = s.into == nullptr;
  s.time = 0.f;
  playerAnim->PortInvalidatePose();
}

void End() {
  if (s.showing && s.target != nullptr) {
    const_cast<CAnimData*>(s.target)->PortInvalidatePose();
  }
  s.showing = false;
  s.target = nullptr;
}

void Tick(float dt) {
  if (!s.showing) {
    return;
  }
  s.time += dt;
  if (!s.inLoop && s.time >= Duration(*s.into)) {
    s.time -= Duration(*s.into);
    s.inLoop = true;
  }
  const_cast<CAnimData*>(s.target)->PortInvalidatePose();
}

bool Showing() { return s.showing; }

CVector3f Centre() { return CVector3f(s.file.centre.x, s.file.centre.y, s.file.centre.z); }

void Override(const CAnimData& data, const CSegIdList& segs, CSegStatementSet& set,
              const CCharLayoutInfo& layout) {
  if (!s.showing || &data != s.target) {
    return;
  }
  const EA::Baked& clip = s.inLoop ? *s.loop : *s.into;
  uint32_t a = 0;
  uint32_t b = 0;
  float f = 0.f;
  EA::FrameAt(clip, s.time, s.inLoop, a, b, f);
  // Every posed segment back to the skeleton's offset (a clip before may have moved it).
  for (int i = 0; i < int(segs.size()); ++i) {
    const CSegId& seg = segs[i];
    if (seg.val() != 3) {
      set.Set(seg, layout.GetFromParentUnrotated(seg));
    }
  }
  const size_t n = clip.segs.size();
  for (size_t i = 0; i < n; ++i) {
    const CSegId seg(int(clip.segs[i]));
    if (!layout.ContainsSegment(seg)) {
      continue;
    }
    const EA::Quat q = EA::Blend(clip.rot[a * n + i], clip.rot[b * n + i], f);
    set.Set(seg, CQuaternion(q.w, q.x, q.y, q.z));
  }
  const CSegId root(int(clip.offsetSeg));
  if (layout.ContainsSegment(root)) {
    const EA::Vec3& p = clip.off[a];
    const EA::Vec3& q = clip.off[b];
    set.Set(root, CVector3f(p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f, p.z + (q.z - p.z) * f));
  }
}

} // namespace PortScrewBody
