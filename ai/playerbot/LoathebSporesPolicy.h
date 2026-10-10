#pragma once

// Pure decision rules for the Loatheb fight, spores only (mod-playerbots
// parity: NaxxActions_Loatheb.cpp spore assignment + spots). The donor's
// heal-suppression (Necrotic Aura) does NOT exist in vanilla — the
// vanilla kit is Inevitable Doom 29204 / Fungal Bloom 29232 / spore
// 16286, and Doom-window healing is a fresh design, out of scope.
// Verified in tw_world + core boss_loatheb.cpp: boss 16011.

namespace ai
{
// Spore assignment: kill the spore only when standing on it (1yd —
// Fungal Bloom rewards the killer); otherwise stay on the boss.
inline constexpr float kLoathebSporeKillRange = 1.0f;

inline bool ShouldKillLoathebSpore(bool sporeUp, float sporeDist)
{
    return sporeUp && sporeDist <= kLoathebSporeKillRange;
}
} // namespace ai
