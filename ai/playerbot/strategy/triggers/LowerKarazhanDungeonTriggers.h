#pragma once
#include "playerbot/strategy/triggers/DungeonTriggers.h"

namespace ai
{
class LowerKarazhanEnterDungeonTrigger : public EnterDungeonTrigger
{
public:
    LowerKarazhanEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter lower karazhan", "lower karazhan", 532) {}
};

class LowerKarazhanLeaveDungeonTrigger : public LeaveDungeonTrigger
{
public:
    LowerKarazhanLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave lower karazhan", "lower karazhan", 532) {}
};

class KarazhanCryptEnterDungeonTrigger : public EnterDungeonTrigger
{
public:
    KarazhanCryptEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter karazhan crypt", "karazhan crypt", 800) {}
};

class KarazhanCryptLeaveDungeonTrigger : public LeaveDungeonTrigger
{
public:
    KarazhanCryptLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave karazhan crypt", "karazhan crypt", 800) {}
};

class AraxxnaStartFightTrigger : public StartBossFightTrigger
{
public:
    AraxxnaStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start araxxna fight", "araxxna", 61221) {}
};

class AraxxnaEndFightTrigger : public EndBossFightTrigger
{
public:
    AraxxnaEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end araxxna fight", "araxxna", 61221) {}
};

class MoroesStartFightTrigger : public StartBossFightTrigger
{
public:
    MoroesStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start moroes fight", "moroes", 61225) {}
};

class MoroesEndFightTrigger : public EndBossFightTrigger
{
public:
    MoroesEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end moroes fight", "moroes", 61225) {}
};

// Araxxna swarm (core SpawnEggs): a live Skitterweb Egg (30008, hatches
// after 20s) or hatched spider (Crawler 61206 / Darkfang 61207 / Leaper
// 61209) near the bot means cleave them down. DPS only. Reads cached
// values, never a grid scan of its own.
class AraxxnaSwarmTrigger : public Trigger
{
public:
    AraxxnaSwarmTrigger(PlayerbotAI* ai) : Trigger(ai, "araxxna swarm", 2) {}
    bool IsActive() override;
};

// Moroes smoke bomb (57096) blinds sight lines: tanks re-target via tank
// assist while the cloud is up on them (DPS must not pick up loose mobs).
class MoroesVanishedTrigger : public Trigger
{
public:
    MoroesVanishedTrigger(PlayerbotAI* ai) : Trigger(ai, "moroes vanished", 1) {}
    std::string GetTargetName() override { return "self target"; }
    bool IsActive() override { return PlayerbotAI::IsTank(bot) && ai->HasAura(57096, bot); }
};
}
