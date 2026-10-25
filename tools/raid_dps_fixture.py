"""Small-group world seam for encounter-selector fixtures after the DPS tournament port.

These cases use living NPCs in range, without skull marks or upstream-only
unkillable encounters. Keep the actual GeneralDpsStrategy implementation and
bucket policy; stub group composition, distance, and the unused class routes.
The standalone DPS policy suite covers class-specific bucket ordering.
"""
from pathlib import Path
from tools.test_nefarian_bone_target import function


def tournament(source):
    policy = (Path(__file__).resolve().parents[1] / 'ai/playerbot/policy/combat/DpsTargetPolicy.h').read_text()
    return policy.replace('#pragma once', '') + r'''
using ai::GeneralTargetBucket;
unsigned NearGroupCount(PlayerbotAI*, Player*) { return 1; }
bool IsCasterBot(PlayerbotAI*, Player*) { return false; }
bool IsComboBot(PlayerbotAI*, Player*) { return false; }
bool IsWastedDpsTarget(PlayerbotAI*, Unit*) { return false; }
class DpsTournamentStrategy : public DpsTargetStrategy {
public:
    DpsTournamentStrategy(PlayerbotAI* ai, float) : DpsTargetStrategy(ai) {}
    bool IsMoon(Unit* unit) { return IsCcTarget(unit); }
    bool CheckSkull(Unit*) { return false; }
    bool InRange(Unit*) const { return true; }
    float Lifetime(Unit* unit) const { return float(unit->health); }
};
''' + function(source, 'class GeneralDpsStrategy :') + r''';
using CasterDpsStrategy = GeneralDpsStrategy;
using ComboDpsStrategy = GeneralDpsStrategy;
'''


def world(fixture):
    """Supply only core APIs absent from the original tiny fixture worlds."""
    unit = function(fixture, 'struct Unit {')
    extra = ''
    if 'IsPlayer()' not in unit:
        extra += ' bool IsPlayer() const { return false; }\n'
    if 'IsAlive()' not in unit:
        extra += ' bool IsAlive() const { return true; }\n'
    fixture = fixture.replace('struct Unit {', 'struct Unit {\n'+extra, 1)
    start = fixture.index('struct Player ')
    opening = fixture.index('{', start)
    fixture = fixture[:opening+1] + '\n    float GetDistance(Unit*) const { return 0; }\n' + fixture[opening+1:]
    # All extracted selectors read estimated group DPS even for a small group.
    # Explicit specializations must appear before the production extraction.
    fixture += '\ntemplate<> float AiObjectContext::Value<float>(std::string) { return 1.0f; }\n'
    # These encounter cases have no Horsemen rotation or timed CC fallback.
    fixture = fixture.replace('struct TargetValue {', 'struct TargetValue { Unit* FindLastCcTarget() { return nullptr; }')
    fixture += '\nUnit* FindLastCcTarget() { return nullptr; }\n'
    fixture += '\nUnit* FourHorsemenRotationTarget(PlayerbotAI*, Player*) { return nullptr; }\n'
    return fixture
