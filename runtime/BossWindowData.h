#pragma once

// The breath windows of bosses whose breath is cast triggered, for the dashboard's boss overlay (runtime/BossWindowSignal.h).
// DATA, not code: a boss without rows here never opens a window, so another boss adds rows, never a branch in the game
// code. Dashboard only: no bot reads this table.
//
// One row per hover point: the boss stops at the point, wears the window aura, has no target, and breathes the point's
// spell along the lane towards the opposite point for windowMs. Columns:
//
//   map, boss entry, window aura, point id, x, y, radius yd, breath spell, end point id, window ms
//
// Onyxia (entry 10184, map 249: runtime/HireUnlockPolicy.h, the spell_target_position rows of map 249 at the end of the
// script). Core (Tortoise 1.18.1, ~/tortoise-wow, src/scripts/dungeons/onyxias_lair/boss_onyxia.cpp):
// - the points, their spells and their end points are aMoveData (:95-105, struct sOnyxMove :87-93): {uiLocId,
//   uiLocIdEnd, uiSpellId, fX, fY, ...}; the comment on each line names the point;
// - the window aura is SPELL_HOVER 17131 (:54, cast at liftoff :600, removed at landing :628, read by isOnyxiaFlying
//   :334-337);
// - the breath: DoMovement (:501-534) on the Deep Breath roll casts the CURRENT point's uiSpellId triggered (:522), sets
//   m_uiDeepBreathTimer = 5000 (:521), faces the opposite point and clears her target (:524-526); UpdateAI keeps the
//   target clear while m_bDeepBreathIsCasting (:707-708), and the timer's end moves her on to the opposite point (:437-447);
// - a triggered spell never reaches a cast slot (core src/game/Spells/Spell.cpp:3640-3642 and :3667-3669), which is why the
//   module reads this state instead of a current spell.
// The radius is not core data: MovePoint (:442, :532) ends at the point itself, and 8 yd tolerates the position read.
//
// Standard library only, so tools/test_boss_window_signal.cpp compiles it without a game header.

#include <cstdint>

namespace TortoiseBots {
namespace BossWindow {

struct BreathPoint
{
    uint32_t map;
    uint32_t bossEntry;
    uint32_t windowAura;
    uint32_t point;
    float x;
    float y;
    float radius;
    uint32_t spell;
    uint32_t endPoint;
    uint32_t windowMs;
};

inline constexpr BreathPoint kBreathPoints[] = {
    // map, entry, aura, point, x, y, radius, spell, end, ms        boss_onyxia.cpp aMoveData line
    {249, 10184, 17131, 0, 10.2191f, -247.912f, 8.0f, 18617, 4, 5000}, // :97 north-east, SPELL_BREATH_NE_TO_SW (:66)
    {249, 10184, 17131, 1, -31.4963f, -250.123f, 8.0f, 18576, 5, 5000}, // :98 east, SPELL_BREATH_EAST_TO_WEST (:59)
    {249, 10184, 17131, 2, -63.5156f, -240.096f, 8.0f, 18564, 6, 5000}, // :99 south-east, SPELL_BREATH_SE_TO_NW (:62)
    {249, 10184, 17131, 3, -65.8444f, -213.809f, 8.0f, 18351, 7, 5000}, // :100 south, SPELL_BREATH_SOUTH_TO_NORTH (:57)
    {249, 10184, 17131, 4, -58.2509f, -189.020f, 8.0f, 18596, 0, 5000}, // :101 south-west, SPELL_BREATH_SW_TO_NE (:65)
    {249, 10184, 17131, 5, -33.5561f, -182.682f, 8.0f, 18609, 1, 5000}, // :102 west, SPELL_BREATH_WEST_TO_EAST (:60)
    {249, 10184, 17131, 6, 6.8951f, -180.246f, 8.0f, 18584, 2, 5000},   // :103 north-west, SPELL_BREATH_NW_TO_SE (:63)
    {249, 10184, 17131, 7, 22.8763f, -217.152f, 8.0f, 17086, 3, 5000},  // :104 north, SPELL_BREATH_NORTH_TO_SOUTH (:56)
};

} // namespace BossWindow
} // namespace TortoiseBots
