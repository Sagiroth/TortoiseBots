"""Production class-quest filtering with both quest iteration orders."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class ClassQuestFetchTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_priority_before_append(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp").read_text()
        start = source.index("            float questRange = 1000.f")
        end = source.index("\n        }\n    }", start)
        block = source[start:end]
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>
using uint32 = uint32_t;
using int32 = int32_t;
struct Quest {
    bool classQuest;
    uint32 GetRequiredClasses() const { return classQuest ? 1 : 0; }
};
struct ObjectMgr {
    std::map<uint32, Quest> quests;
    Quest const* GetQuestTemplate(uint32 id) { return &quests.at(id); }
} sObjectMgr;
struct Bot { uint32 GetLevel() const { return 20; } } player;
std::vector<uint32> fetch(std::vector<uint32> order, bool onlyClassQuest) {
    Bot* bot = &player;
    std::vector<std::tuple<uint32, int32, float>> destinationFetches{{1,0,2000}};
    for (uint32 questId : order) {
        Quest const* questTemplate = sObjectMgr.GetQuestTemplate(questId);
        uint32 flag = 2;
'''
        harness += block + r'''
    }
    std::vector<uint32> result;
    for (auto const& entry : destinationFetches) result.push_back(std::get<1>(entry));
    return result;
}
int main() {
    sObjectMgr.quests = {{1,{false}}, {2,{true}}, {3,{false}}, {4,{true}}};
    assert(fetch({1,2}, true) == (std::vector<uint32>{0,2}));
    assert(fetch({2,1}, true) == (std::vector<uint32>{0,2}));
    assert(fetch({1,2,3,4}, true) == (std::vector<uint32>{0,2,4}));
    assert(fetch({1,3}, true) == (std::vector<uint32>{0,1,3}));
    assert(fetch({1,2,3,4}, false) == (std::vector<uint32>{0,1,2,3,4}));
    assert(fetch({}, true) == (std::vector<uint32>{0}));
    assert(fetch({2}, true) == (std::vector<uint32>{0,2}));
}
'''
        with tempfile.TemporaryDirectory(prefix="class-quest-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
