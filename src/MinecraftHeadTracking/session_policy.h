#pragma once

namespace mcht::session {

// Head tracking decouples where you look from where you aim, which is the same
// advantage a freelook mod gives. So it switches off whenever PvP is enabled
// and there is another player who could be fought.
//
// The rule is uniform: host, guest, Realm and dedicated server are all treated
// the same, and nobody is exempt. A solo session simply does not satisfy the
// second condition, because there is nobody to fight.
//
// Note the `pvp` game rule alone would be useless as a gate: it defaults to
// true, including in single-player worlds, so gating on it by itself would
// disable the mod nearly everywhere.

// Evaluated from the render thread once per camera setup. Internally throttled
// and cached, so the ~240Hz call rate costs almost nothing.
//
// `self` is the LevelRendererPlayer the camera hook was handed.
//
// Every state it cannot read resolves to "not allowed": no client instance, no
// level, no local player, an implausible player count, or a game-rule vector
// that does not look the way this build's profile says it should. The one
// failure mode worth avoiding is tracking silently staying on during a fight.
bool TrackingAllowed(void* self);

}  // namespace mcht::session
