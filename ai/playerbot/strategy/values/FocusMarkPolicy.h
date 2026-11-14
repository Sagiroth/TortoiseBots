#pragma once
#include <string>
#include <vector>

// Pure rules for the per-bot focus mark (`.bot focus`): which mark a damage
// dealer or tank attacks first, and which orders are refused. No core
// includes, so tools/test_focus_mark_policy.cpp can run it.

namespace ai
{
inline bool IsFocusMarkWord(std::string const& mark)
{
    static char const* const marks[] = { "skull", "cross", "square", "moon", "triangle", "diamond", "circle", "star" };
    for (char const* known : marks)
        if (mark == known)
            return true;
    return false;
}

// The mark to try when the bot's own focus mark is on no reachable mob:
// Skull, unless the bot already focuses Skull or ignores marks ("none").
// Empty means the normal pick follows.
inline std::string FocusFallbackMark(std::string const& ownMark, bool ownMarkUp)
{
    if (ownMarkUp || ownMark == "skull" || !IsFocusMarkWord(ownMark))
        return std::string();
    return "skull";
}

enum class FocusRefusal
{
    None,
    Healer,
    CcMark,
};

// A focus order for one bot. Healers get no focus mark; a mark another
// party bot holds as its CC mark is refused, or the focus fire would break
// the CC (the bot's mark is taken before the pick that skips CC marks).
inline FocusRefusal DecideFocusOrder(bool botIsHealer, std::string const& mark,
    std::vector<std::string> const& heldCcMarks)
{
    if (botIsHealer)
        return FocusRefusal::Healer;
    if (mark == "skull")
        return FocusRefusal::None;
    for (std::string const& cc : heldCcMarks)
        if (cc == mark)
            return FocusRefusal::CcMark;
    return FocusRefusal::None;
}

// The reverse: a CC order on a mark some party bot holds as its focus mark.
// Skull is everyone's default focus and is never a CC mark, so it does not
// count here.
inline bool CcMarkHeldAsFocus(std::string const& ccMark, std::vector<std::string> const& heldFocusMarks)
{
    if (ccMark == "skull")
        return false;
    for (std::string const& focus : heldFocusMarks)
        if (focus == ccMark)
            return true;
    return false;
}
}
