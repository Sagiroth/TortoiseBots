"""Run the production loot-content accessor against per-player loot fixtures (#280)."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class LootContentTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_only_remaining_shared_and_personal_items_are_returned(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        text = (root / "ai/playerbot/strategy/values/LootValues.cpp").read_text()
        start = text.index("std::vector<LootItem*> LootAccess::GetLootContentFor(")
        end = text.index("// Get loot status for a specified player.", start)
        method = text[start:end]
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
struct LootAccess {
    Loot const* loot;
    std::vector<LootItem*> GetLootContentFor(Player* player) const;
};
'''
        # The production rule the accessor asks (no core headers, so it builds here).
        fixture += '#include "' + str(root / "ai/playerbot/policy/loot/LootViewRulePolicy.h") + '"\n'
        fixture += method + r'''
int main() {
    Player first{1}, second{2}, absent{3};
    Loot loot;
    // Shared live, shared taken, FFA, conditional, globally taken FFA.
    loot.items = {{false,false,0}, {true,false,0}, {false,true,0},
                  {false,false,7}, {true,true,0}};
    loot.m_questItems = {{false,false,0}, {true,false,0}, {false,false,0}};
    QuestItemList firstFfa{{2,true}, {4,false}};
    QuestItemList secondFfa{{2,false}, {4,false}};
    QuestItemList firstConditional{{3,true}};
    QuestItemList secondConditional{{3,false}};
    QuestItemList firstQuest{{0,false}, {1,false}, {2,true}, {99,false}};
    QuestItemList secondQuest{{0,true}, {1,false}, {2,false}};
    loot.ffa = {{1,&firstFfa}, {2,&secondFfa}};
    loot.conditional = {{1,&firstConditional}, {2,&secondConditional}};
    loot.quests = {{1,&firstQuest}, {2,&secondQuest}, {3,nullptr}};
    LootAccess access{&loot};
    assert((access.GetLootContentFor(&first) ==
            std::vector<LootItem*>{&loot.items[0], &loot.m_questItems[0]}));
    assert((access.GetLootContentFor(&second) ==
            std::vector<LootItem*>{&loot.items[0], &loot.m_questItems[2],
                                  &loot.items[2], &loot.items[3]}));
    assert((access.GetLootContentFor(&absent) == std::vector<LootItem*>{&loot.items[0]}));
    // Without a player, retain remaining non-quest content for generic consumers.
    assert((access.GetLootContentFor(nullptr) ==
            std::vector<LootItem*>{&loot.items[0], &loot.items[2], &loot.items[3]}));
    access.loot = nullptr;
    assert(access.GetLootContentFor(&first).empty());
    access.loot = &loot;
    loot.items[0].is_looted = true;
    firstQuest[0].is_looted = true;
    assert(access.GetLootContentFor(&first).empty());
    // Another player's remaining copies must still be available.
    assert(access.GetLootContentFor(&second).size() == 3);
    // The round-robin reservation: while another member holds the turn and has a
    // quest list on the corpse, the viewer's non-FFA quest copies are hidden.
    loot.items[0].is_looted = false;
    loot.m_questItems[0].is_looted = false;
    firstQuest[0].is_looted = false;
    loot.roundRobinPlayer = 2;
    assert((access.GetLootContentFor(&first) == std::vector<LootItem*>{&loot.items[0]}));
    // The holder of the turn sees its own copies again (m_questItems[1] is taken).
    loot.roundRobinPlayer = 1;
    assert((access.GetLootContentFor(&first) ==
            std::vector<LootItem*>{&loot.items[0], &loot.m_questItems[0]}));
}
'''
        with tempfile.TemporaryDirectory(prefix="loot-content-") as folder:
            source = pathlib.Path(folder) / "test.cpp"
            binary = pathlib.Path(folder) / "test"
            source.write_text(fixture)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
