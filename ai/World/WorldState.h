#pragma once
// Vanilla/Turtle has no WotLK world-state expansion API. The compatibility
// shim supplies the minimal expansion query for code that is explicitly gated
// away by MANGOSBOT_ZERO.
#ifndef WORLDSTATE_ADDED
#define WORLDSTATE_ADDED
struct WorldStateHeadless { int GetExpansion() const { return 0; } };
using WorldState = WorldStateHeadless;
inline WorldStateHeadless sWorldState;
#endif
