"""Wiring pins for AiPlayerbot.InterruptAllowStun: a stun counts as an interrupt.

The rule is pure and tested in tools/test_interrupt_casting_policy.cpp (stun_interrupts_cast, stun_needs_a_cast,
stun_immune_no, the key-off answer and the trigger-resolved Intercept stun). These pins prove the live code feeds it:
the policy needs the key and the stun effect / aura ids, the glue resolves Intercept's triggered stun and reads
stun immunity on the triggered spell, the key ships off in the config and the docs.

Bash, Hammer of Justice, War Stomp and Repentance are an APPLY_AURA MOD_STUN effect only on this core, so the donor's
three-way check never accepted them and the tank interrupt rows were dead (Tortoise Trials tank-bear-bash-caster 0/75,
tank-paladin-hoj-caster 3/32).

Run: python3 -m unittest tools/test_stun_interrupt_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/InterruptCastingPolicy.h").read_text()
SPELLS = (ROOT / "ai/playerbot/PlayerbotAISpells.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
COMBAT_SETTINGS = (ROOT / "docs/server/settings/combat.md").read_text()
BOSS_DOC = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def brace_body(text, signature):
    """The brace-matched body that follows `signature` in `text`."""
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    raise AssertionError("unbalanced braces after %s" % signature)


class InterruptAllowStun(unittest.TestCase):
    """AiPlayerbot.InterruptAllowStun: a stun the target is not immune to counts as an interrupt."""

    def test_the_key_ships_off(self):
        self.assertIn("bool interruptAllowStun = false;", CONFIG_H)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.InterruptAllowStun", false)', CONFIG_CPP)
        self.assertIn("\nAiPlayerbot.InterruptAllowStun = 0\n", CONF)
        # Reloadable with the other interrupt keys, so a BEFORE/AFTER trial needs no restart.
        self.assertIn('config.GetBoolDefault("AiPlayerbot.InterruptAllowStun", false)',
                      brace_body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()"))

    def test_the_policy_takes_the_key_and_the_stun_ids(self):
        self.assertIn("INTERRUPT_AURA_MOD_STUN = 12,", POLICY)
        signature = brace_body(
            POLICY,
            "inline bool ShouldInterruptTargetCasting(InterruptTargetSpell const* genericSpell,\n"
            "        InterruptTargetSpell const* channeledSpell, InterruptSpellInfo const& interrupt, bool allowStun)")
        # The stun branch exists only behind the key, and never for a target that cannot be stunned.
        stun = brace_body(signature, "bool stun = false;\n        if (allowStun && !interrupt.stunImmunity.BlocksStun())")
        self.assertIn("if (allowStun && !interrupt.stunImmunity.BlocksStun())", signature)
        self.assertIn("interrupt.effect[i] == INTERRUPT_EFFECT_APPLY_AURA &&\n"
                      "                        interrupt.effectAura[i] == INTERRUPT_AURA_MOD_STUN", stun)
        self.assertIn("interrupt.effectTriggeredStun[i]", stun)
        self.assertIn("if (interrupt.effectImmune[i])\n                    continue;", stun)
        # A stun skips the victim's silence/interrupt-flag gate, which is the point of accepting it.
        gate = signature[signature.index("static const uint32_t SILENCE = 1;"):signature.index("static const uint32_t INTERRUPT_FLAG_COMBAT")]
        self.assertIn("if (!stun)\n        {\n            if (victim->preventionType != SILENCE)", gate)
        self.assertIn("else if (!(victim->interruptFlags & INTERRUPT_FLAG_DAMAGE))", gate)
        # The donor's three-way check stands; the stun is the fourth way, after it.
        self.assertIn("return stun;\n    }", signature)
        self.assertNotIn("return false;\n    }", signature)

    def test_the_glue_passes_the_key_and_resolves_a_triggered_stun(self):
        glue = brace_body(SPELLS, "bool PlayerbotAI::IsInterruptableSpellCasting(Unit* target, std::string spell, "
                                  "uint8 effectMask)")
        self.assertIn("bool const allowStun = sPlayerbotAIConfig.interruptAllowStun;", glue)
        self.assertIn("return ai::ShouldInterruptTargetCasting(genericSlot, channeledSlot, interrupt, allowStun);",
                      glue)
        # Intercept 20252 effect 2 (TRIGGER_SPELL); it triggers 20253, the MOD_STUN aura.
        trigger = brace_body(glue, "if (allowStun && spellInfo->Effect[i] == SPELL_EFFECT_TRIGGER_SPELL)")
        self.assertIn("sServerFacade.LookupSpellInfo(spellInfo->EffectTriggerSpell[i])", trigger)
        self.assertIn("triggered->Effect[j] == SPELL_EFFECT_APPLY_AURA", trigger)
        self.assertIn("triggered->EffectApplyAuraName[j] == SPELL_AURA_MOD_STUN", trigger)
        # Immunity is read on the triggered spell's own spell and effect, so a stun-immune target refuses.
        self.assertIn("!target->IsImmuneToSpell(triggered, false)", trigger)
        self.assertIn("!target->IsImmuneToSpellEffect(triggered, (SpellEffectIndex)j, true)", trigger)
        self.assertIn("interrupt.effectTriggeredStun[i] = true;", trigger)
        # One level only: the trigger lookup is inside the effect loop, not recursive.
        self.assertIn("for (int32 j = EFFECT_INDEX_0; j <= EFFECT_INDEX_2; ++j)", trigger)

    def test_the_glue_refuses_a_target_that_cannot_be_stunned(self):
        """Any one of the five immunity answers blocks the stun; bosses need no immunity bit."""
        glue = brace_body(SPELLS, "bool PlayerbotAI::IsInterruptableSpellCasting(Unit* target, std::string spell, "
                                  "uint8 effectMask)")
        immunity = brace_body(glue, "if (allowStun)\n    {")
        # 1. The spell-level check reads the spell's Mechanic (Bash/HoJ stun is there, not on the effect).
        self.assertIn("target->IsImmuneToSpell(spellInfo, false)", immunity)
        # 2. The creature's raw mechanic_immune_mask, so a missing spell Mechanic cannot slip through.
        self.assertIn("ai::StunMechanicImmune(info->mechanic_immune_mask)", immunity)
        # 4. A boss counts as unstunnable without the bit: world boss, plan boss, boss rank.
        self.assertIn("creature->IsWorldBoss()", immunity)
        self.assertIn("info->rank == CREATURE_ELITE_WORLDBOSS", immunity)
        self.assertIn("ai::GetDungeonPlans().IsBossEntry(creature->GetMapId(), creature->GetEntry())", immunity)
        # 5. An existing stun aura or stun diminishing return at immune.
        self.assertIn("target->GetAurasByType(SPELL_AURA_MOD_STUN).empty()", immunity)
        self.assertIn("StunDiminished(target, spellInfo, false)", immunity)
        # The DR helper covers the stun groups only.
        dr_groups = brace_body(SPELLS, "bool IsStunDiminishingGroup(DiminishingGroup group)")
        self.assertIn("DIMINISHING_CONTROL_STUN", dr_groups)
        self.assertIn("DIMINISHING_TRIGGER_STUN", dr_groups)
        self.assertIn("DIMINISHING_KIDNEYSHOT", dr_groups)
        dr = brace_body(SPELLS, "bool StunDiminished(Unit* target, SpellEntry const* stunSpell, bool triggered)")
        self.assertIn("target->GetDiminishing(group) == DIMINISHING_LEVEL_IMMUNE", dr)
        # The rule asks the flag before looking for a stun.
        self.assertIn("if (allowStun && !interrupt.stunImmunity.BlocksStun())", POLICY)
        self.assertIn("bool BlocksStun() const", POLICY)
        self.assertIn("INTERRUPT_MECHANIC_STUN = 12", POLICY)
        self.assertIn("(mechanicImmuneMask & (1u << (INTERRUPT_MECHANIC_STUN - 1))) != 0", POLICY)

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.InterruptAllowStun`", COMBAT_SETTINGS)
        self.assertIn("(#aiplayerbotinterruptallowstun)", COMBAT_SETTINGS)
        section = COMBAT_SETTINGS[COMBAT_SETTINGS.index("### `AiPlayerbot.InterruptAllowStun`"):]
        self.assertIn("MOD_STUN", section)
        self.assertIn("20252", section)
        self.assertIn("stun-immune", section)
        self.assertIn("InterruptAllowStun", BOSS_DOC)
        self.assertIn("InterruptAllowStun", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
