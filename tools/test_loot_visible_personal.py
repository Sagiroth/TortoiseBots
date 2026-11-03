"""Behaviour tests for the hidden personal copy (TB #1307).

ai::HasVisiblePersonalItemFor (ai/playerbot/policy/loot/CorpseLootPolicy.h)
restricts Loot::hasItemFor to the copies the core actually shows this viewer:
while another member holds the round-robin turn and has a copy in the same
quest/conditional list, the viewer's non-FFA copy stays hidden. The text pins
in tools/test_loot_view_rule_wiring.py cannot see a flipped comparison, so
this compiles the production function against stub Loot/Player types (in the
style of tools/test_loot_content.py) and runs the turn cases through it.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class VisiblePersonalItemTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_reserved_personal_copy_is_hidden_from_the_other_member(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        helper = (root / "ai/playerbot/policy/loot/CorpseLootPolicy.h").read_text()
        start = helper.index("inline bool HasVisiblePersonalItemFor(")
        method = helper[start:helper.index("\n}", start)]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <vector>
using uint8 = uint8_t;
using uint32 = uint32_t;
struct ObjectGuid {
    uint64_t value;
    ObjectGuid(uint64_t v) : value(v) {}
    operator uint64_t() const { return value; }
    uint32 GetCounter() const { return uint32(value); }
};
struct Player { uint32 guid; uint32 GetGUIDLow() const { return guid; } ObjectGuid GetGUID() const { return ObjectGuid(guid); } };
struct LootItem { bool is_looted = false; bool freeforall = false; uint32 conditionId = 0; };
struct QuestItem { uint8 index; bool is_looted; };
using LootItemList = std::vector<LootItem>;
using QuestItemList = std::vector<QuestItem>;
using QuestItemMap = std::map<uint32, QuestItemList*>;
struct Loot {
    LootItemList items, m_questItems;
    QuestItemMap quests, ffa, conditional;
    uint64_t roundRobinPlayer = 0;
    QuestItemMap const& GetPlayerQuestItems() const { return quests; }
    QuestItemMap const& GetPlayerFFAItems() const { return ffa; }
    QuestItemMap const& GetPlayerNonQuestNonFFAConditionalItems() const { return conditional; }
};
'''
        fixture += '#include "' + str(root / "ai/playerbot/policy/loot/LootViewRulePolicy.h") + '"\n'
        fixture += "namespace ai {\n" + method + "\n}\n" + r'''
int main() {
    Player bot{1}, other{2};
    Loot loot;
    // Quest copies live apart from shared items: slot 0 is taken in the
    // shared store but live in the quest store, so reading the quest copy
    // from the wrong store shows up here.
    loot.m_questItems = {{false, false, 0}};
    loot.items = {{true, false, 0}, {false, true, 0}, {false, false, 9}};
    QuestItemList botQuest{{0, false}}, otherQuest{{0, false}};
    QuestItemList botFfa{{1, false}};
    QuestItemList botCond{{2, false}}, otherCond{{2, false}};
    QuestItemMap botQuestOnly{{1, &botQuest}};
    QuestItemMap bothQuest{{1, &botQuest}, {2, &otherQuest}};
    QuestItemMap botFfaOnly{{1, &botFfa}};
    QuestItemMap botCondOnly{{1, &botCond}};
    QuestItemMap bothCond{{1, &botCond}, {2, &otherCond}};
    QuestItemMap none{};
    auto visible = [&](QuestItemMap const& q, QuestItemMap const& f,
                       QuestItemMap const& c, uint64_t turn) {
        loot.quests = q;
        loot.ffa = f;
        loot.conditional = c;
        loot.roundRobinPlayer = turn;
        return ai::HasVisiblePersonalItemFor(loot, &bot);
    };
    // P3: another member holds the turn and has a quest copy -> hidden.
    assert(visible(bothQuest, none, none, other.GetGUID()) == false);
    // P1: the turn holder has no quest copy -> shown.
    assert(visible(botQuestOnly, none, none, other.GetGUID()) == true);
    // Released turn -> shown.
    assert(visible(bothQuest, none, none, 0) == true);
    // Own turn -> shown.
    assert(visible(bothQuest, none, none, bot.GetGUID()) == true);
    // FFA copies ignore the turn.
    assert(visible(none, botFfaOnly, none, other.GetGUID()) == true);
    // Conditional copies follow the same rule.
    assert(visible(none, none, bothCond, other.GetGUID()) == false);
    assert(visible(none, none, botCondOnly, other.GetGUID()) == true);
    assert(visible(none, none, bothCond, 0) == true);
    assert(visible(none, none, bothCond, bot.GetGUID()) == true);
}
'''
        with tempfile.TemporaryDirectory(prefix="loot-visible-personal-") as folder:
            source = pathlib.Path(folder) / "test.cpp"
            binary = pathlib.Path(folder) / "test"
            source.write_text(fixture)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
