// Screw Attack diagnostics: where the last screw attack was triggered, where each of its jumps
// started and where it ended, to compare the Prime 1 port's Screw Attack with Echoes' by the
// numbers (the same file is in both ports; F1 shows Summary()). World units; distances are
// horizontal, heights from the trigger point. Game thread only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace PortScrewDiag {

struct Point {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
};

constexpr int kMaxJumps = 8;

struct Run {
  bool active = false;
  bool done = false;
  Point trigger;
  Point firstJump;
  Point lastJump;
  Point end;
  int jumps = 0;
  float peak = 0.f;
  // Horizontal distance from each jump to the next one, or to the end for the last.
  float jumpDistance[kMaxJumps] = {};
};

inline Run& Current() {
  static Run run;
  return run;
}

inline Run& Last() {
  static Run run;
  return run;
}

inline float Flat(const Point& a, const Point& b) { return std::hypot(b.x - a.x, b.y - a.y); }

// The press that starts the attack.
inline void Trigger(float x, float y, float z) {
  Run& run = Current();
  run = Run();
  run.active = true;
  run.trigger = {x, y, z};
  run.lastJump = run.trigger;
}

// A jump of the chain, at the ball's position when it was given.
inline void Jump(float x, float y, float z) {
  Run& run = Current();
  if (!run.active) {
    return;
  }
  const Point at{x, y, z};
  if (run.jumps == 0) {
    run.firstJump = at;
  } else if (run.jumps <= kMaxJumps) {
    run.jumpDistance[run.jumps - 1] = Flat(run.lastJump, at);
  }
  run.lastJump = at;
  ++run.jumps;
}

// Every tick of the attack, for the peak.
inline void Track(float z) {
  Run& run = Current();
  if (run.active) {
    run.peak = std::max(run.peak, z - run.trigger.z);
  }
}

// The attack ended here (a hit, a mistimed press, falling past the limit).
inline void End(float x, float y, float z) {
  Run& run = Current();
  if (!run.active) {
    return;
  }
  run.end = {x, y, z};
  if (run.jumps > 0 && run.jumps <= kMaxJumps) {
    run.jumpDistance[run.jumps - 1] = Flat(run.lastJump, run.end);
  }
  run.active = false;
  run.done = true;
  Last() = run;
}

inline std::string Summary() {
  const Run& run = Last();
  if (!run.done) {
    return "Last screw attack: none yet";
  }
  char line[160];
  std::string out;
  std::snprintf(line, sizeof(line), "Last screw attack: %d jumps, %.2f from trigger to end, peak +%.2f\n",
                run.jumps, Flat(run.trigger, run.end), run.peak);
  out += line;
  if (run.jumps == 0) {
    return out;
  }
  std::snprintf(line, sizeof(line), "  wind-up (trigger to first jump) %.2f\n",
                Flat(run.trigger, run.firstJump));
  out += line;
  out += "  per jump:";
  for (int i = 0; i < std::min(run.jumps, kMaxJumps); ++i) {
    std::snprintf(line, sizeof(line), " %.2f", run.jumpDistance[i]);
    out += line;
  }
  out += "\n";
  std::snprintf(line, sizeof(line), "  drop at end %.2f below the trigger\n", run.trigger.z - run.end.z);
  out += line;
  return out;
}

} // namespace PortScrewDiag
