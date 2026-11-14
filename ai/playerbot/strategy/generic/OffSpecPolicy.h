#pragma once
#include <cstdint>
#include <string>

// Pure rules for the off-spec switches (`offheal` on a damage-spec hybrid,
// `offdps` on a healer). No core includes, so tools/test_off_spec_policy.cpp
// can run it.

namespace ai
{
// Only these classes register the offheal/offdps strategies (CLASS_* ids:
// paladin, priest, shaman, druid).
inline bool ClassHasOffSpec(std::uint8_t classId)
{
    return classId == 2 || classId == 5 || classId == 7 || classId == 11;
}

// The off-spec part a `.bot role <spec>` adds after the spec kit, so a spec
// change lands on the same off-spec default AiFactory gives that spec.
// role uses the BOT_ROLE_* bits: 1 tank, 2 healer, 4 dps. Empty for classes
// without off-spec strategies.
inline std::string OffSpecChangeForRole(std::uint8_t classId, std::uint8_t role, bool offSpecEnabled)
{
    if (!ClassHasOffSpec(classId))
        return std::string();
    if (offSpecEnabled && role == 2)
        return "+offdps,-offheal";
    if (offSpecEnabled && role == 4)
        return "+offheal,-offdps";
    return "-offheal,-offdps";
}
}
