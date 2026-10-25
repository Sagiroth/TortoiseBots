"""Production quest-link cleanup must skip unavailable quest templates."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class QuestFilterMissingTemplateTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_missing_template_is_not_formatted(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/ChatFilter.cpp").read_text()
        cls = source.index("class QuestChatFilter")
        helpers = source[source.index("    inline std::string toLower", cls):source.index("    virtual std::string Filter", cls)]
        start = source.index("                for (auto& questId : questIds)", cls)
        brace = source.index("{", start)
        depth = 1
        end = brace + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        block = source[start:end]
        formatter = (root / "ai/playerbot/ChatHelper.cpp").read_text()
        a = formatter.index("std::string ChatHelper::formatQuest(Quest const* quest)")
        method = formatter[a:formatter.index("std::string ChatHelper::formatGameobject", a)]
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using uint32 = uint32_t;
struct Quest {
    uint32 id;
    std::string title;
    std::string GetTitle() const { return title; }
    uint32 GetQuestId() const { return id; }
    int GetQuestLevel() const { return 1; }
};
struct QuestLocale { std::vector<std::string> Title; };
struct ObjectMgr {
    std::map<uint32, Quest> quests{{1,{1,"Known quest"}}};
    Quest const* GetQuestTemplate(uint32 id) {
        auto it = quests.find(id);
        return it == quests.end() ? nullptr : &it->second;
    }
    QuestLocale const* GetQuestLocale(uint32) { return nullptr; }
} sObjectMgr;
struct TextMgr { int GetLocalePriority() { return -1; } } sPlayerbotTextMgr;
struct ChatHelper { static std::string formatQuest(Quest const*); };
'''
        harness += method + "struct Filter {\n" + helpers
        harness += "std::string clean(std::string message, std::set<uint32> questIds) {\n" + block
        harness += r'''
    return message;
}
};
int main() {
    Filter filter;
    std::string known = ChatHelper::formatQuest(sObjectMgr.GetQuestTemplate(1));
    Quest retired{99, "Old quest"};
    std::string oldLink = ChatHelper::formatQuest(&retired);
    assert(filter.clean("@quest=" + known + " " + oldLink + " follow", {1,99}) == "@quest= " + oldLink + " follow");
    std::string old = "@quest=" + oldLink + " follow";
    assert(filter.clean(old, {99}) == old);
    assert(filter.clean("@quest=" + known + " stay", {1}) == "@quest= stay");
    assert(filter.clean("follow", {}) == "follow");
}
'''
        with tempfile.TemporaryDirectory(prefix="quest-filter-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
