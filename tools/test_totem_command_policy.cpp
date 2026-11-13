#include "../ai/playerbot/strategy/shaman/ShamanTotemCommandPolicy.h"
#include "../ai/playerbot/policy/class/ShamanRecallPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
        std::exit(1); \
    } \
} while (0)

using ai::NormalizeTotemOrder;
using ai::OrderedTotemForSlot;
using ai::ParseTotemDropMode;
using ai::ParseTotemSetList;
using ai::TotemAutoRowsFire;
using ai::TotemicRecallShouldFire;
using ai::TotemDropMode;
using ai::TotemDropModeName;
using ai::TotemDropWantsSlot;
using ai::TotemModeKey;
using ai::TotemOrderKey;
using ai::TotemSlotIndex;
using ai::TotemSlotName;

int main()
{
    // Slots resolve by name, case handled by the caller (lowered).
    CHECK(TotemSlotIndex("earth") == 0);
    CHECK(TotemSlotIndex("fire") == 1);
    CHECK(TotemSlotIndex("water") == 2);
    CHECK(TotemSlotIndex("air") == 3);
    CHECK(TotemSlotIndex("mana") == -1);
    CHECK(std::string(TotemSlotName(0)) == "earth");
    CHECK(std::string(TotemSlotName(3)) == "air");
    CHECK(std::string(TotemSlotName(9)) == "");
    std::puts("  [PASS] slot names resolve, unknown rejected");

    // Order keys are stable per-bot manual values.
    CHECK(std::string(TotemOrderKey(0)) == std::string("totem order earth"));
    CHECK(std::string(TotemOrderKey(3)) == std::string("totem order air"));
    CHECK(std::string(TotemModeKey()) == std::string("totem drop mode"));
    std::puts("  [PASS] order keys stable");

    // Modes parse; unknown falls back to auto (caller ERRs first, this never
    // silently invents a mode).
    CHECK(ParseTotemDropMode("auto") == TotemDropMode::Auto);
    CHECK(ParseTotemDropMode("prepull") == TotemDropMode::Prepull);
    CHECK(ParseTotemDropMode("pre-pull") == TotemDropMode::Prepull);
    CHECK(ParseTotemDropMode("manual") == TotemDropMode::Manual);
    CHECK(ParseTotemDropMode("whenever") == TotemDropMode::Auto);
    CHECK(std::string(TotemDropModeName(TotemDropMode::Prepull)) == "prepull");
    CHECK(std::string(TotemDropModeName(TotemDropMode::Manual)) == "manual");
    CHECK(std::string(TotemDropModeName(TotemDropMode::Auto)) == "auto");
    std::puts("  [PASS] drop modes parse, names round-trip");

    // Orders normalize: auto/none pass, canonical totems pass for their
    // slot only, anything else is rejected. Short names (with or without
    // "totem", caller already lowered) resolve to the canonical spell.
    bool valid = false;
    CHECK(NormalizeTotemOrder(3, "auto", valid) == "auto" && valid);
    CHECK(NormalizeTotemOrder(3, "none", valid) == "" && valid);
    CHECK(NormalizeTotemOrder(3, "windfury totem", valid) == "windfury totem" && valid);
    CHECK(NormalizeTotemOrder(3, "tranquil air totem", valid) == "tranquil air totem" && valid);
    CHECK(NormalizeTotemOrder(2, "mana spring totem", valid) == "mana spring totem" && valid);
    CHECK(NormalizeTotemOrder(0, "strength of earth totem", valid) == "strength of earth totem" && valid);
    CHECK(NormalizeTotemOrder(1, "searing totem", valid) == "searing totem" && valid);
    CHECK(NormalizeTotemOrder(2, "mana spring", valid) == "mana spring totem" && valid);
    CHECK(NormalizeTotemOrder(3, "windfury", valid) == "windfury totem" && valid);
    CHECK(NormalizeTotemOrder(3, "tranquil air", valid) == "tranquil air totem" && valid);
    CHECK(NormalizeTotemOrder(0, "strength of earth", valid) == "strength of earth totem" && valid);
    // Cross-slot totem rejected: windfury is not a water totem.
    NormalizeTotemOrder(2, "windfury totem", valid);
    CHECK(!valid);
    NormalizeTotemOrder(2, "windfury", valid);
    CHECK(!valid);
    // Unknown word rejected.
    NormalizeTotemOrder(3, "wrath of air totem", valid);
    CHECK(!valid);
    NormalizeTotemOrder(3, "wrath of air", valid);
    CHECK(!valid);
    std::puts("  [PASS] orders validate per slot, cross-slot and unknown rejected");

    // Explicit orders win; auto/none fall back to the subgroup pick.
    CHECK(OrderedTotemForSlot("windfury totem") == "windfury totem");
    CHECK(OrderedTotemForSlot("auto") == "");
    CHECK(OrderedTotemForSlot("") == "");
    std::puts("  [PASS] explicit orders win, auto falls back");

    // Auto rows: combat or the pre-pull window; never OOC otherwise.
    // auto and prepull share the rule (owner decision 2026-10-21: no totems
    // out of combat in the default mode); manual never fires.
    CHECK(!TotemAutoRowsFire(TotemDropMode::Auto, false, false));
    CHECK(TotemAutoRowsFire(TotemDropMode::Auto, true, false));
    CHECK(TotemAutoRowsFire(TotemDropMode::Auto, false, true));
    CHECK(TotemAutoRowsFire(TotemDropMode::Auto, true, true));
    CHECK(TotemAutoRowsFire(TotemDropMode::Prepull, false, true));
    CHECK(TotemAutoRowsFire(TotemDropMode::Prepull, true, false));
    CHECK(!TotemAutoRowsFire(TotemDropMode::Prepull, false, false));
    CHECK(!TotemAutoRowsFire(TotemDropMode::Manual, true, true));
    CHECK(!TotemAutoRowsFire(TotemDropMode::Manual, false, false));
    std::puts("  [PASS] auto rows fire per mode");

    // The rows and the recall refund never fight over the same set: while the
    // rows are armed (combat or the pre-pull window) the refund stands down,
    // and out of combat with no fight imminent the rows are quiet, so the
    // recalled set is not rebuilt until the next fight — no drop/recall loop
    // for any mode, and nothing latches on combat for a flicker to clear.
    for (TotemDropMode mode : {TotemDropMode::Auto, TotemDropMode::Prepull, TotemDropMode::Manual})
    {
        for (bool inCombat : {false, true})
        {
            for (bool imminent : {false, true})
            {
                bool const rows = TotemAutoRowsFire(mode, inCombat, imminent);
                bool const recall = TotemicRecallShouldFire(true, true, false, inCombat, false, !inCombat && imminent);
                CHECK(!(rows && recall));
            }
        }
    }
    std::puts("  [PASS] armed rows and the recall refund never fight over the set");

    // Drop queues free slots with a wanted totem, skips occupied/none.
    CHECK(TotemDropWantsSlot(false, "mana spring totem"));
    CHECK(!TotemDropWantsSlot(true, "mana spring totem"));
    CHECK(!TotemDropWantsSlot(false, ""));
    std::puts("  [PASS] drop queues free slots only");

    // `set` takes exactly four comma-separated words, each valid for its slot.
    {
        std::string out[4];
        CHECK(ParseTotemSetList("strength of earth totem,searing totem,mana spring totem,windfury totem", out));
        CHECK(out[0] == "strength of earth totem" && out[1] == "searing totem");
        CHECK(out[2] == "mana spring totem" && out[3] == "windfury totem");
        CHECK(ParseTotemSetList("  Strength Of Earth , SEARING,mana spring ,Windfury  ", out));
        CHECK(out[0] == "strength of earth totem" && out[1] == "searing totem");
        CHECK(out[2] == "mana spring totem" && out[3] == "windfury totem");
        CHECK(ParseTotemSetList("auto,none,auto,none", out));
        CHECK(out[0] == "auto" && out[1].empty() && out[2] == "auto" && out[3].empty());
        CHECK(!ParseTotemSetList("auto,auto,auto", out));
        CHECK(!ParseTotemSetList("auto,auto,auto,auto,auto", out));
        CHECK(!ParseTotemSetList("auto,auto,,auto", out));
        CHECK(!ParseTotemSetList("", out));
        CHECK(!ParseTotemSetList("searing,auto,auto,auto", out));
        CHECK(!ParseTotemSetList("auto,auto,auto,mana spring", out));
    }
    std::puts("  [PASS] set list parses four valid words, rejects the rest");

    std::puts("All totem command policy checks PASSED!");
    {
        std::string const orders[4] = { "strength of earth totem", "auto", "", "windfury totem" };
        CHECK(!ai::TotemBlockedBySlotOrder("strength of earth totem", orders));
        CHECK(ai::TotemBlockedBySlotOrder("earthbind totem", orders));
        CHECK(!ai::TotemBlockedBySlotOrder("tremor totem", orders));
        CHECK(ai::TotemBlockedBySlotOrder("tranquil air totem", orders));
        CHECK(!ai::TotemBlockedBySlotOrder("magma totem", orders));
        CHECK(!ai::TotemBlockedBySlotOrder("mana spring totem", orders));
    }

    return 0;
}
