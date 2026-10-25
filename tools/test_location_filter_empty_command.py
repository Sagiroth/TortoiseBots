"""Compile the production named-filter slicer with empty-command inputs."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class LocationFilterEmptyCommandTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_selector_without_command(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/ChatFilter.cpp").read_text()
        start = source.index("std::string ChatFilter::Filter(std::string message, std::string filter)")
        method = source[start:source.index("class StrategyChatFilter", start)]
        harness = r'''
#include <cassert>
#include <string>
struct ChatFilter { std::string Filter(std::string, std::string); };
'''
        harness += method + r'''
int main() {
    ChatFilter filter;
    assert(filter.Filter("@kalimdor", "@kalimdor").empty());
    assert(filter.Filter("@dun morogh", "@dun morogh").empty());
    assert(filter.Filter("@kalimdor ", "@kalimdor").empty());
    assert(filter.Filter("@kalimdor follow", "@kalimdor") == "follow");
    assert(filter.Filter("@dun morogh stay", "@dun morogh") == "stay");
    assert(filter.Filter("follow", "@kalimdor") == "follow");
    assert(filter.Filter("", "@kalimdor").empty());
    assert(filter.Filter("@mage follow", "") == "follow");
}
'''
        with tempfile.TemporaryDirectory(prefix="location-filter-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
