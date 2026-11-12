// The Screw Attack's spinning Samus (Screw Attack, CMorphBall): while the attack is on, Prime's
// in-game Samus model (the player's own, BallTransitionsANCS) is drawn in place of the ball, posed
// by Echoes' screw attack clips: the curl into the spin, then the spin, looped. The clips come
// from the player's Echoes disc, retargeted onto Prime's skeleton by the Echoes import
// (port_echoes_anim.h, echoes_screw_attack.bin in the user folder); without that file the ball
// shows as before. Game thread only.
#pragma once

#include <string>

class CAnimData;
class CCharLayoutInfo;
class CSegIdList;
class CSegStatementSet;
class CVector3f;

namespace PortScrewBody {

// Whether the clips are loaded (read once, the first time this is asked).
bool Available();
// "3 clips (...)", or why there are none.
std::string Status();
// Reads the file again (after an import).
void Reload();

// The attack's start and end, and each tick of it in between, for the player's animation data
// (whose pose this replaces).
void Begin(CAnimData* playerAnim);
void End();
void Tick(float dt);
// Whether the body stands in for the ball now.
bool Showing();
// The middle of the spinning body in the model's own space (unscaled): drawn on the ball's centre.
CVector3f Centre();

// CAnimData::RecalcPoseBuilder, after the animation tree's statements: the clips' rotations in
// their place for the player's animation data while Showing(), each other segment's offset back
// to the skeleton's and Skeleton_Root's from the clip.
void Override(const CAnimData& data, const CSegIdList& segs, CSegStatementSet& set,
              const CCharLayoutInfo& layout);

} // namespace PortScrewBody
