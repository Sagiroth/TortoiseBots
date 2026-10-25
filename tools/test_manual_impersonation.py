"""Production manual impersonation dispatch without an LLM connection."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class ManualImpersonationTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_dispatch_and_empty_payload(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/strategy/actions/RpgSubActions.cpp").read_text()
        start = source.index('    else if (line == "impersonate"')
        end = source.index('    else if (line == "continue")', start)
        block = source[start:end]
        harness = r'''
#include <cassert>
#include <string>
int stateChanges = 0;
#define SET_AI_VALUE2(...) (++stateChanges)
#define SET_GAI_VALUE2(...) ((void)0)
constexpr int LANG_UNIVERSAL = 0;
struct Unit {
    std::string name, speech, emote;
    std::string GetName() const { return name; }
    int GetInstanceId() const { return 0; }
    void MonsterSay(char const* text, int, Unit*) { speech = text; }
    void MonsterTextEmote(char const* text, Unit*) { emote = text; }
};
struct Target {
    Unit* unit;
    Unit* GetUnit(int) { return unit; }
};
Unit speaker{"Companion", "", ""}, npc{"Innkeeper", "", ""};
Unit* bot = &speaker;
Target target{&npc};
std::string llmContext;
int fallback = 0;
void dispatch(std::string const& line) {
    if (false) {}
'''
        harness += block + r'''
    else ++fallback;
}
int main() {
    dispatch("impersonate");
    dispatch("impersonate ");
    assert(fallback == 0 && npc.speech.empty() && npc.emote.empty());
    dispatch("impersonate Welcome.");
    assert(npc.speech == "Welcome." && fallback == 0);
    assert(llmContext == " Innkeeper:Welcome.");
    dispatch("impersonate *waves*");
    assert(npc.emote == "*waves*" && fallback == 0);
    assert(llmContext == " Innkeeper:Welcome. Innkeeper:*waves*");
    dispatch("ordinary chat");
    dispatch("impersonated");
    assert(fallback == 2);
    target.unit = nullptr;
    int changesBefore = stateChanges;
    std::string historyBefore = llmContext;
    dispatch("impersonate Welcome.");
    assert(stateChanges == changesBefore && llmContext == historyBefore);
    assert(fallback == 2);
}
'''
        with tempfile.TemporaryDirectory(prefix="manual-chat-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
