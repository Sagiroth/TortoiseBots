// Forward-ported from mod-playerbots Shaman/Strategy/TotemsShamanStrategy.h
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TOTEMSSHAMANSTRATEGY_H
#define PLAYERBOTS_TOTEMSSHAMANSTRATEGY_H

#include "GenericShamanStrategy.h"
#include <string>
#include <vector>

// This is the header with all of the totem-related constants and arrays used in the Shaman strategies.

// Totem Bar Slot Constants
#define TOTEM_BAR_SLOT_FIRE 132
#define TOTEM_BAR_SLOT_EARTH 133
#define TOTEM_BAR_SLOT_WATER 134
#define TOTEM_BAR_SLOT_AIR 135

// Strength of Earth Totem
static const uint32 STRENGTH_OF_EARTH_TOTEM[] = {
    58643,  // rank 8
    57622,  // rank 7
    25528,  // rank 6
    25361,  // rank 5
    10442,  // rank 4
    8161,   // rank 3
    8160,   // rank 2
    8075    // rank 1
};
static const size_t STRENGTH_OF_EARTH_TOTEM_COUNT = sizeof(STRENGTH_OF_EARTH_TOTEM) / sizeof(uint32);

// Stoneskin Totem
static const uint32 STONESKIN_TOTEM[] = {
    58753,  // rank 10
    58751,  // rank 9
    25509,  // rank 8
    25508,  // rank 7
    10408,  // rank 6
    10407,  // rank 5
    10406,  // rank 4
    8155,   // rank 3
    8154,   // rank 2
    8071    // rank 1
};
static const size_t STONESKIN_TOTEM_COUNT = sizeof(STONESKIN_TOTEM) / sizeof(uint32);

// Tremor Totem
static const uint32 TREMOR_TOTEM[] = {
    8143  // rank 1
};
static const size_t TREMOR_TOTEM_COUNT = sizeof(TREMOR_TOTEM) / sizeof(uint32);

// Earthbind Totem
static const uint32 EARTHBIND_TOTEM[] = {
    2484  // rank 1
};
static const size_t EARTHBIND_TOTEM_COUNT = sizeof(EARTHBIND_TOTEM) / sizeof(uint32);

// Stoneclaw Totem
static const uint32 STONECLAW_TOTEM[] = {
    58582,  // rank 10
    58581,  // rank 9
    58580,  // rank 8
    25525,  // rank 7
    10428,  // rank 6
    10427,  // rank 5
    6392,   // rank 4
    6391,   // rank 3
    6390,   // rank 2
    5730    // rank 1
};
static const size_t STONECLAW_TOTEM_COUNT = sizeof(STONECLAW_TOTEM) / sizeof(uint32);

// Searing Totem
static const uint32 SEARING_TOTEM[] = {
    58704,  // rank 10
    58703,  // rank 9
    58699,  // rank 8
    25533,  // rank 7
    10438,  // rank 6
    10437,  // rank 5
    6365,   // rank 4
    6364,   // rank 3
    6363,   // rank 2
    3599    // rank 1
};
static const size_t SEARING_TOTEM_COUNT = sizeof(SEARING_TOTEM) / sizeof(uint32);

// Magma Totem
static const uint32 MAGMA_TOTEM[] = {
    58734,  // rank 7
    58731,  // rank 6
    25552,  // rank 5
    10587,  // rank 4
    10586,  // rank 3
    10585,  // rank 2
    8190    // rank 1
};
static const size_t MAGMA_TOTEM_COUNT = sizeof(MAGMA_TOTEM) / sizeof(uint32);

// Flametongue Totem
static const uint32 FLAMETONGUE_TOTEM[] = {
    58656,  // rank 8
    58652,  // rank 7
    58649,  // rank 6
    25557,  // rank 5
    16387,  // rank 4
    10526,  // rank 3
    8249,   // rank 2
    8227    // rank 1
};
static const size_t FLAMETONGUE_TOTEM_COUNT = sizeof(FLAMETONGUE_TOTEM) / sizeof(uint32);

// Totem of Wrath
static const uint32 TOTEM_OF_WRATH[] = {
    57722,  // rank 4
    57721,  // rank 3
    57720,  // rank 2
    30706   // rank 1
};
static const size_t TOTEM_OF_WRATH_COUNT = sizeof(TOTEM_OF_WRATH) / sizeof(uint32);

// Frost Resistance Totem
static const uint32 FROST_RESISTANCE_TOTEM[] = {
    58745,  // rank 6
    58741,  // rank 5
    25560,  // rank 4
    10479,  // rank 3
    10478,  // rank 2
    8181    // rank 1
};
static const size_t FROST_RESISTANCE_TOTEM_COUNT = sizeof(FROST_RESISTANCE_TOTEM) / sizeof(uint32);

// Fire Elemental Totem
static const uint32 FIRE_ELEMENTAL_TOTEM[] = {
    2894  // rank 1
};
static const size_t FIRE_ELEMENTAL_TOTEM_COUNT = sizeof(FIRE_ELEMENTAL_TOTEM) / sizeof(uint32);

// Healing Stream Totem
static const uint32 HEALING_STREAM_TOTEM[] = {
    58757,  // rank 9
    58756,  // rank 8
    58755,  // rank 7
    25567,  // rank 6
    10463,  // rank 5
    10462,  // rank 4
    6377,   // rank 3
    6375,   // rank 2
    5394    // rank 1
};
static const size_t HEALING_STREAM_TOTEM_COUNT = sizeof(HEALING_STREAM_TOTEM) / sizeof(uint32);

// Mana Spring Totem
static const uint32 MANA_SPRING_TOTEM[] = {
    58774,  // rank 8
    58773,  // rank 7
    58771,  // rank 6
    25570,  // rank 5
    10497,  // rank 4
    10496,  // rank 3
    10495,  // rank 2
    5675    // rank 1
};
static const size_t MANA_SPRING_TOTEM_COUNT = sizeof(MANA_SPRING_TOTEM) / sizeof(uint32);

// Cleansing Totem
static const uint32 CLEANSING_TOTEM[] = {
    8170  // rank 1
};
static const size_t CLEANSING_TOTEM_COUNT = sizeof(CLEANSING_TOTEM) / sizeof(uint32);

// Fire Resistance Totem
static const uint32 FIRE_RESISTANCE_TOTEM[] = {
    58739,  // rank 6
    58737,  // rank 5
    25563,  // rank 4
    10538,  // rank 3
    10537,  // rank 2
    8184    // rank 1
};
static const size_t FIRE_RESISTANCE_TOTEM_COUNT = sizeof(FIRE_RESISTANCE_TOTEM) / sizeof(uint32);

// Mana Tide Totem
static const uint32 MANA_TIDE_TOTEM[] = {
    16190  // rank 1
};
static const size_t MANA_TIDE_TOTEM_COUNT = sizeof(MANA_TIDE_TOTEM) / sizeof(uint32);

// Wrath of Air Totem
static const uint32 WRATH_OF_AIR_TOTEM[] = {
    3738  // rank 1
};
static const size_t WRATH_OF_AIR_TOTEM_COUNT = sizeof(WRATH_OF_AIR_TOTEM) / sizeof(uint32);

// Windfury Totem
static const uint32 WINDFURY_TOTEM[] = {
    8512  // rank 1
};
static const size_t WINDFURY_TOTEM_COUNT = sizeof(WINDFURY_TOTEM) / sizeof(uint32);

// Nature Resistance Totem
static const uint32 NATURE_RESISTANCE_TOTEM[] = {
    58749,  // rank 6
    58746,  // rank 5
    25574,  // rank 4
    10601,  // rank 3
    10600,  // rank 2
    10595   // rank 1
};
static const size_t NATURE_RESISTANCE_TOTEM_COUNT = sizeof(NATURE_RESISTANCE_TOTEM) / sizeof(uint32);

// Grounding Totem
static const uint32 GROUNDING_TOTEM[] = {
    8177  // rank 1
};
static const size_t GROUNDING_TOTEM_COUNT = sizeof(GROUNDING_TOTEM) / sizeof(uint32);

class PlayerbotAI;

// Earth Totem Strategies
class StrengthOfEarthTotemStrategy : public GenericShamanStrategy
{
public:
    StrengthOfEarthTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "strength of earth"; }
};

class StoneskinTotemStrategy : public GenericShamanStrategy
{
public:
    StoneskinTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "stoneskin"; }
};

class EarthTotemStrategy : public GenericShamanStrategy
{
public:
    EarthTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "tremor"; }
};

class EarthbindTotemStrategy : public GenericShamanStrategy
{
public:
    EarthbindTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "earthbind"; }
};

// Fire Totem Strategies
class SearingTotemStrategy : public GenericShamanStrategy
{
public:
    SearingTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "searing"; }
};

class MagmaTotemStrategy : public GenericShamanStrategy
{
public:
    MagmaTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "magma"; }
};

class FlametongueTotemStrategy : public GenericShamanStrategy
{
public:
    FlametongueTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "flametongue"; }
};

class TotemOfWrathStrategy : public GenericShamanStrategy
{
public:
    TotemOfWrathStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "wrath"; }
};

class FrostResistanceTotemStrategy : public GenericShamanStrategy
{
public:
    FrostResistanceTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "frost resistance"; }
};

// Water Totem Strategies
class HealingStreamTotemStrategy : public GenericShamanStrategy
{
public:
    HealingStreamTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "healing stream"; }
};

class ManaSpringTotemStrategy : public GenericShamanStrategy
{
public:
    ManaSpringTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "mana spring"; }
};

class CleansingTotemStrategy : public GenericShamanStrategy
{
public:
    CleansingTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "cleansing"; }
};

class FireResistanceTotemStrategy : public GenericShamanStrategy
{
public:
    FireResistanceTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "fire resistance"; }
};

// Air Totem Strategies
class WrathOfAirTotemStrategy : public GenericShamanStrategy
{
public:
    WrathOfAirTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "wrath of air"; }
};

class WindfuryTotemStrategy : public GenericShamanStrategy
{
public:
    WindfuryTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "windfury"; }
};

class NatureResistanceTotemStrategy : public GenericShamanStrategy
{
public:
    NatureResistanceTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "nature resistance"; }
};

class GroundingTotemStrategy : public GenericShamanStrategy
{
public:
    GroundingTotemStrategy(PlayerbotAI* botAI);
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "grounding"; }
};

#endif
