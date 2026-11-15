#include "../ai/playerbot/InterruptCastingPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::InterruptSpellInfo;
using ai::InterruptTargetSpell;
using ai::ShouldInterruptTargetCasting;

// The key as shipped: AiPlayerbot.InterruptAllowStun = 0, the donor's answers.
static const bool kStunOff = false;
// AiPlayerbot.InterruptAllowStun = 1.
static const bool kStunOn = true;

// A pummel/kick-like interrupt spell: interrupt-cast effect, silence prevention.
static InterruptSpellInfo Pummel()
{
    InterruptSpellInfo info;
    info.interruptFlags = 0x04; // SPELL_INTERRUPT_FLAG_INTERRUPT
    info.preventionType = 1;    // SILENCE
    info.effect[0] = ai::INTERRUPT_EFFECT_INTERRUPT_CAST;
    return info;
}

// Bash 5211 / Hammer of Justice 853 on this core: one APPLY_AURA MOD_STUN
// effect, no interrupt flag, prevention type 2 (magic).
static InterruptSpellInfo Bash()
{
    InterruptSpellInfo info;
    info.interruptFlags = 0;
    info.preventionType = 2; // SPELL_PREVENTION_TYPE_MAGIC
    info.effect[0] = ai::INTERRUPT_EFFECT_APPLY_AURA;
    info.effectAura[0] = ai::INTERRUPT_AURA_MOD_STUN;
    return info;
}

// A silence-prevented 2.5 s heal on its cast bar.
static InterruptTargetSpell BarCasting()
{
    InterruptTargetSpell s;
    static int marker = 1;
    s.spell = reinterpret_cast<Spell const*>(&marker);
    s.channeledSlot = false;
    s.state = ai::INTERRUPT_SPELL_PREPARING;
    s.castTime = 2500;
    s.preventionType = 1; // SILENCE
    s.interruptFlags = 0x02; // DAMAGE
    return s;
}

// The Mistwing Rogue's Lightning Bolt from the bear trial: generic slot, a
// cast bar, silence-prevented, damage interrupt flag.
static InterruptTargetSpell LightningBolt()
{
    return BarCasting();
}

int main()
{
    std::cout << "Starting TortoiseBots interrupt-casting policy tests...\n";
    InterruptSpellInfo pummel = Pummel();

    // Live case (Patchwerk 2026-10-14): melee boss, no spell slots at all.
    CHECK(!ShouldInterruptTargetCasting(nullptr, nullptr, pummel, kStunOff));
    std::cout << "  [PASS] idle boss is not interruptible\n";

    // Real interruptible cast: heal on its bar.
    InterruptTargetSpell bar = BarCasting();
    CHECK(ShouldInterruptTargetCasting(&bar, nullptr, pummel, kStunOff));
    std::cout << "  [PASS] cast-bar heal is interruptible\n";

    // Instant with a missile in flight (DELAYED, e.g. Hateful Strike):
    // nothing on a bar to interrupt.
    InterruptTargetSpell delayed = BarCasting();
    delayed.state = 5; // SPELL_STATE_DELAYED
    CHECK(!ShouldInterruptTargetCasting(&delayed, nullptr, pummel, kStunOff));
    std::cout << "  [PASS] delayed instant is not interruptible\n";

    // Instant with no cast time sitting in PREPARING: same, no bar.
    InterruptTargetSpell instant = BarCasting();
    instant.castTime = 0;
    CHECK(!ShouldInterruptTargetCasting(&instant, nullptr, pummel, kStunOff));
    std::cout << "  [PASS] zero-cast-time preparing spell is not interruptible\n";

    // Finished slot leftovers are not casts.
    InterruptTargetSpell finished = BarCasting();
    finished.state = 3; // SPELL_STATE_FINISHED
    CHECK(!ShouldInterruptTargetCasting(&finished, nullptr, pummel, kStunOff));
    std::cout << "  [PASS] finished spell is not interruptible\n";

    // Channeled spell in flight is interruptible when channel-flagged.
    InterruptTargetSpell channel = BarCasting();
    channel.channeledSlot = true;
    channel.state = ai::INTERRUPT_SPELL_CASTING;
    channel.channelInterruptFlags = 0x04; // CHANNEL_FLAG_INTERRUPT
    CHECK(ShouldInterruptTargetCasting(nullptr, &channel, pummel, kStunOff));
    std::cout << "  [PASS] channeled cast is interruptible\n";

    // A non-silence-prevented cast (enrage, prevention type 0) cannot be
    // interrupted by pummel/kick.
    InterruptTargetSpell enrage = BarCasting();
    enrage.preventionType = 0;
    CHECK(!ShouldInterruptTargetCasting(&enrage, nullptr, pummel, kStunOff));
    std::cout << "  [PASS] non-silence cast is not interruptible\n";

    // Immunity to the interrupt effect refuses.
    InterruptSpellInfo immunePummel = Pummel();
    immunePummel.effectImmune[0] = true;
    CHECK(!ShouldInterruptTargetCasting(&bar, nullptr, immunePummel, kStunOff));
    std::cout << "  [PASS] immune target is not interruptible\n";

    // AiPlayerbot.InterruptAllowStun = 1 changes none of the donor answers.
    CHECK(!ShouldInterruptTargetCasting(nullptr, nullptr, pummel, kStunOn));
    CHECK(ShouldInterruptTargetCasting(&bar, nullptr, pummel, kStunOn));
    CHECK(ShouldInterruptTargetCasting(nullptr, &channel, pummel, kStunOn));
    CHECK(!ShouldInterruptTargetCasting(&enrage, nullptr, pummel, kStunOn));
    CHECK(!ShouldInterruptTargetCasting(&bar, nullptr, immunePummel, kStunOn));
    std::cout << "  [PASS] the key on keeps every interrupt-spell answer\n";

    // stun_interrupts_cast (fails on the old code): Bash's APPLY_AURA
    // MOD_STUN against the Lightning Bolt the bear trial's caster started.
    InterruptSpellInfo bash = Bash();
    InterruptTargetSpell bolt = LightningBolt();
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bash, kStunOff));
    CHECK(ShouldInterruptTargetCasting(&bolt, nullptr, bash, kStunOn));
    std::cout << "  [PASS] stun_interrupts_cast\n";

    // stun_needs_a_cast: a stun still needs a cast on a bar.
    CHECK(!ShouldInterruptTargetCasting(nullptr, nullptr, bash, kStunOn));
    InterruptTargetSpell boltDelayed = LightningBolt();
    boltDelayed.state = 5; // SPELL_STATE_DELAYED
    CHECK(!ShouldInterruptTargetCasting(&boltDelayed, nullptr, bash, kStunOn));
    InterruptTargetSpell boltInstant = LightningBolt();
    boltInstant.castTime = 0;
    CHECK(!ShouldInterruptTargetCasting(&boltInstant, nullptr, bash, kStunOn));
    std::cout << "  [PASS] stun_needs_a_cast\n";

    // stun_immune_no: the stun-immune target (IsImmuneToSpellEffect on the
    // MOD_STUN effect) keeps the answer no.
    InterruptSpellInfo bashImmune = Bash();
    bashImmune.effectImmune[0] = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bashImmune, kStunOn));
    std::cout << "  [PASS] stun_immune_no\n";

    // stun_spell_mechanic_immune: IsImmuneToSpell refused the stun spell itself
    // (Bash/Hammer of Justice carry STUN as the spell Mechanic, the effect
    // mechanic is empty, so IsImmuneToSpellEffect alone missed it).
    InterruptSpellInfo bashSpellImmune = Bash();
    bashSpellImmune.stunImmunity.spellImmune = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bashSpellImmune, kStunOn));
    std::cout << "  [PASS] stun_spell_mechanic_immune\n";

    // stun_mechanic_mask_immune: the creature's mechanic_immune_mask has the
    // STUN bit, with the stun only in the spell's Mechanic. Read directly, so
    // a missing effect mechanic cannot slip through (fails before the fix: the
    // rule only saw the empty effect mechanic).
    static const uint32_t STUN_BIT = 1u << (ai::INTERRUPT_MECHANIC_STUN - 1);
    InterruptSpellInfo bashMaskImmune = Bash();
    bashMaskImmune.stunImmunity.stunMechanicImmune = ai::StunMechanicImmune(STUN_BIT);
    CHECK(bashMaskImmune.stunImmunity.stunMechanicImmune);
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bashMaskImmune, kStunOn));
    // Any other mask bit does not read as STUN immunity.
    CHECK(!ai::StunMechanicImmune(STUN_BIT - 1));
    CHECK(!ai::StunMechanicImmune(0));
    std::cout << "  [PASS] stun_mechanic_mask_immune\n";

    // stun_boss_no: a world boss or the run's plan boss counts as unstunnable
    // even without the STUN bit.
    InterruptSpellInfo bashBoss = Bash();
    bashBoss.stunImmunity.boss = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bashBoss, kStunOn));
    std::cout << "  [PASS] stun_boss_no\n";

    // stun_already_stunned_no / stun_diminished_no: a stun that cannot land
    // for these reasons is no interrupt either.
    InterruptSpellInfo bashStunned = Bash();
    bashStunned.stunImmunity.alreadyStunned = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bashStunned, kStunOn));
    InterruptSpellInfo bashDiminished = Bash();
    bashDiminished.stunImmunity.stunDiminished = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bashDiminished, kStunOn));
    std::cout << "  [PASS] stunned or stun-diminished target takes no stun\n";

    // stun_normal_elite_yes: no immunity flag and no boss status (a normal
    // elite caster) still gets the stun pick with the key on.
    InterruptSpellInfo bashNormal = Bash();
    CHECK(!bashNormal.stunImmunity.BlocksStun());
    CHECK(ShouldInterruptTargetCasting(&bolt, nullptr, bashNormal, kStunOn));
    std::cout << "  [PASS] stun_normal_elite_yes\n";

    // The key off keeps refusing every stun, immune flags or not.
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bashMaskImmune, kStunOff));
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, bashBoss, kStunOff));
    std::cout << "  [PASS] the key off still refuses every stun\n";

    // A stun stops a cast the victim's silence prevention does not cover
    // (the core interrupts any non-melee cast on a stun).
    InterruptTargetSpell notSilencePrevented = LightningBolt();
    notSilencePrevented.preventionType = 0;
    notSilencePrevented.interruptFlags = 0;
    CHECK(!ShouldInterruptTargetCasting(&notSilencePrevented, nullptr, bash, kStunOff));
    CHECK(ShouldInterruptTargetCasting(&notSilencePrevented, nullptr, bash, kStunOn));
    std::cout << "  [PASS] a stun skips the victim's silence gate\n";

    // Intercept 20252: its charge effect (64) triggers 20253, the MOD_STUN
    // aura. The caller resolves that into effectTriggeredStun.
    InterruptSpellInfo intercept;
    intercept.preventionType = 2;
    intercept.effect[1] = 64; // SPELL_EFFECT_TRIGGER_SPELL
    intercept.effectTriggeredStun[1] = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, intercept, kStunOff));
    CHECK(ShouldInterruptTargetCasting(&bolt, nullptr, intercept, kStunOn));
    // The triggered spell's own immunity answer arrives as effectImmune.
    InterruptSpellInfo interceptImmune = intercept;
    interceptImmune.effectImmune[1] = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, interceptImmune, kStunOn));
    // The same target-level immunity (STUN bit, boss) refuses the triggered stun.
    InterruptSpellInfo interceptMaskImmune = intercept;
    interceptMaskImmune.stunImmunity.stunMechanicImmune = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, interceptMaskImmune, kStunOn));
    InterruptSpellInfo interceptBoss = intercept;
    interceptBoss.stunImmunity.boss = true;
    CHECK(!ShouldInterruptTargetCasting(&bolt, nullptr, interceptBoss, kStunOn));
    std::cout << "  [PASS] a triggered stun counts\n";

    std::cout << "All interrupt-casting policy tests passed.\n";
    return 0;
}
