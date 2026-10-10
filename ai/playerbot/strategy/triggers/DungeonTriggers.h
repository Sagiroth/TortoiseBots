#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class EnterDungeonTrigger : public Trigger
    {
    public:
        // You can get the mapID from worlddb > instance_template > map column
        // or from here https://wow.tools/dbc/?dbc=map&build=1.12.1.5875
        EnterDungeonTrigger(PlayerbotAI* ai, std::string name, std::string dungeonStrategy, uint32 mapID)
        : Trigger(ai, name, 5)
        , dungeonStrategy(dungeonStrategy)
        , mapID(mapID) {}

        bool IsActive() override;

    private:
        std::string dungeonStrategy;
        uint32 mapID;
    };

    class LeaveDungeonTrigger : public Trigger
    {
    public:
        // You can get the mapID from worlddb > instance_template > map column
        // or from here https://wow.tools/dbc/?dbc=map&build=1.12.1.5875
        LeaveDungeonTrigger(PlayerbotAI* ai, std::string name, std::string dungeonStrategy, uint32 mapID)
        : Trigger(ai, name, 5)
        , dungeonStrategy(dungeonStrategy)
        , mapID(mapID) {}

        bool IsActive() override;

    private:
        std::string dungeonStrategy;
        uint32 mapID;
    };

    class StartBossFightTrigger : public Trigger
    {
    public:
        StartBossFightTrigger(PlayerbotAI* ai, std::string name, std::string bossStrategy, uint64 bossID)
        : Trigger(ai, name, 1)
        , bossStrategy(bossStrategy)
        , bossID(bossID) {}

        bool IsActive() override;

    private:
        std::string bossStrategy;
        uint64 bossID;
    };

    class EndBossFightTrigger : public Trigger
    {
    public:
        EndBossFightTrigger(PlayerbotAI* ai, std::string name, std::string bossStrategy, uint64 bossID)
        : Trigger(ai, name, 5)
        , bossStrategy(bossStrategy)
        , bossID(bossID) {}

        bool IsActive() override;

    private:
        std::string bossStrategy;
        uint64 bossID;
    };

    class CloseToHazardTrigger : public Trigger
    {
    public:
        CloseToHazardTrigger(PlayerbotAI* ai, std::string name, int checkInterval, float hazardRadius, time_t hazardDuration)
        : Trigger(ai, name, checkInterval)
        , hazardRadius(hazardRadius)
        , hazardDuration(hazardDuration) {}

        bool IsActive() override final;

    protected:
        virtual std::list<ObjectGuid> GetPossibleHazards() = 0;
        virtual bool IsHazardValid(const ObjectGuid& hazzardGuid);

    private:
        float GetDistanceToHazard(const ObjectGuid& hazzardGuid);

    protected:
        float hazardRadius;
        time_t hazardDuration;
    };

    // Universal raid survival: bomb/plague runout. Baron Geddon Living Bomb
    // (20475), Vaelastrasz Burning Adrenaline (23620 + classic 18173),
    // Grobbulus Mutating Injection (28169) all detonate into the raid clump
    // unless the carrier runs 30yd clear. Spell-ID based so it fires on the
    // carrier regardless of which boss applied it.
    class RaidBombDebuffTrigger : public Trigger
    {
    public:
        RaidBombDebuffTrigger(PlayerbotAI* ai, std::string name = "raid bomb debuff", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Per-boss resist auras (mod-playerbots parity, fight-agnostic):
    // while ANY attacker matches the fire/shadow boss list, paladins swap
    // to the matching resistance aura. Paladin-gated first (cheap class
    // check filters out most of the raid, mirroring the donor), then a
    // bounded attacker-list scan for boss entries — no world scan.
    class BossWantsFireAuraTrigger : public Trigger
    {
    public:
        BossWantsFireAuraTrigger(PlayerbotAI* ai, std::string name = "boss wants fire aura", int checkInterval = 5)
        : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };

    class BossWantsShadowAuraTrigger : public Trigger
    {
    public:
        BossWantsShadowAuraTrigger(PlayerbotAI* ai, std::string name = "boss wants shadow aura", int checkInterval = 5)
        : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };

    // Legacy 4H mark threshold alert. ReactionStrategy routes this to the
    // generic hazard escape; it does not rotate targets, assign tanks, or
    // ensure that moving away is safe. Marks: 28832-28835.
    class FourHorsemenMarkTrigger : public Trigger
    {
    public:
        FourHorsemenMarkTrigger(PlayerbotAI* ai, std::string name = "four horsemen mark", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Universal raid survival: dragon breath/tail geometry. Onyxia (10184),
    // Ebonroc (14601), Flamegor (11981), Firemaw (11983), Nefarian (11583).
    // Tanks hold the head away from the raid; everyone else clears the
    // frontal cone and the rear tail cone and works the flanks.
    class DragonBreathRiskTrigger : public Trigger
    {
    public:
        DragonBreathRiskTrigger(PlayerbotAI* ai, std::string name = "dragon breath risk", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "current target"; }
        bool IsActive() override;
    };

    // Universal raid survival: ranged AoE spread. When another friendly
    // player is inside 10yd during a flagged spread encounter, split out
    // so chain abilities cannot bracket the whole caster line.
    class RaidSpreadNeededTrigger : public Trigger
    {
    public:
        RaidSpreadNeededTrigger(PlayerbotAI* ai, std::string name = "raid spread needed", int checkInterval = 2)
        : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Opt-in combat spread gate ("spread" strategy): fires when another
    // live groupmate is inside the spread radius. Unlike the pool-only
    // raid-spread gate above, ownership and ranged-only never gate — the
    // player asked for spacing. Combat-only, hold orders veto.
    class SpreadNeededTrigger : public Trigger
    {
    public:
        SpreadNeededTrigger(PlayerbotAI* ai, std::string name = "spread needed", int checkInterval = 2)
        : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    class CloseToGameObjectHazardTrigger : public CloseToHazardTrigger
    {
    public:
        CloseToGameObjectHazardTrigger(PlayerbotAI* ai, std::string name, uint32 gameObjectID, float radius, time_t expirationTime)
        : CloseToHazardTrigger(ai, name, 1, radius, expirationTime)
        , gameObjectID(gameObjectID) {}

    private:
        std::list<ObjectGuid> GetPossibleHazards() override;

    private:
        uint32 gameObjectID;
    };

    // Not dungeon-specific despite living in this file - lives here because it wants the
    // same "add hazard"/HazardsValue plumbing CloseToHazardTrigger uses. Detects any nearby
    // GAMEOBJECT_TYPE_TRAP GameObject that auto-fires environmental damage (no owner, spell
    // effect SPELL_EFFECT_ENVIRONMENTAL_DAMAGE - matches the exact condition
    // GameObject::Update uses to hit any player in range, src/game/Objects/GameObject.cpp:459-538),
    // e.g. overworld braziers, generically - no per-entry allowlist needed. Registered on the
    // reaction engine (see ReactionStrategy) so it fires regardless of the bot's
    // combat/non-combat state.
    //
    // Doesn't subclass CloseToHazardTrigger (unlike the other hazard triggers below) because
    // that base class uses one fixed radius for every hazard it tracks - here each GameObject
    // has its own real danger radius (goInfo->trap.radius, the same value the core engine
    // itself uses to decide who to hit), so this reacts at the actual danger boundary instead
    // of a guessed constant, and registers that same accurate radius into "hazards" so
    // pathing (MovementAction::GeneratePathAvoidingHazards) bends around the real zone too.
    class EnvironmentalHazardTrigger : public Trigger
    {
    public:
        EnvironmentalHazardTrigger(PlayerbotAI* ai, std::string name = "environmental hazard nearby", time_t hazardDuration = 60)
        : Trigger(ai, name, 1)
        , hazardDuration(hazardDuration) {}

        bool IsActive() override;

    private:
        time_t hazardDuration;
    };

    class CloseToCreatureHazardTrigger : public CloseToHazardTrigger
    {
    public:
        CloseToCreatureHazardTrigger(PlayerbotAI* ai, std::string name, uint32 creatureID, float radius, time_t expirationTime)
        : CloseToHazardTrigger(ai, name, 1, radius, expirationTime)
        , creatureID(creatureID) {}

    private:
        std::list<ObjectGuid> GetPossibleHazards() override;
        bool IsHazardValid(const ObjectGuid& hazzardGuid) override;

    protected:
        uint32 creatureID;
    };

    class CloseToHostileCreatureHazardTrigger : public CloseToCreatureHazardTrigger
    {
    public:
        CloseToHostileCreatureHazardTrigger(PlayerbotAI* ai, std::string name, uint32 creatureID, float radius, time_t expirationTime)
        : CloseToCreatureHazardTrigger(ai, name, creatureID, radius, expirationTime) {}

    private:
        std::list<ObjectGuid> GetPossibleHazards() override;
    };

    class CloseToCreatureTrigger : public Trigger
    {
    public:
        CloseToCreatureTrigger(PlayerbotAI* ai, std::string name, uint32 creatureID, float range)
        : Trigger(ai, name, 1)
        , creatureID(creatureID)
        , range(range) {}

        bool IsActive() override;

    private:
        uint32 creatureID;
        float range;
    };

    class ItemReadyTrigger : public Trigger
    {
    public:
        ItemReadyTrigger(PlayerbotAI* ai, std::string name, uint32 itemID)
        : Trigger(ai, name, 1)
        , itemID(itemID) {}

        virtual bool IsActive() override;

    protected:
        uint32 itemID;
    };

    class ItemBuffReadyTrigger : public ItemReadyTrigger
    {
    public:
        ItemBuffReadyTrigger(PlayerbotAI* ai, std::string name, uint32 itemID, uint32 buffID)
        : ItemReadyTrigger(ai, name, itemID)
        , buffID(buffID) {}

        bool IsActive() override;

    private:
        uint32 buffID;
    };
}