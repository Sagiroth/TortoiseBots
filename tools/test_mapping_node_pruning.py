"""Production mapping sweep must survive removal of its current node."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class MappingNodePruningTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_removal_during_sweep(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/TravelNode.cpp").read_text()
        method = source.index("void TravelNodeMap::manageNodes(")
        start = source.index("        for (", method)
        block = source[start:source.index("        //Pick random Node", start)]
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>
using std::size_t;
struct Node { int id; bool removable; };
struct Map {
    std::vector<Node*> m_nodes;
    std::vector<int> visited;
    ~Map() { for (Node* n : m_nodes) delete n; }
    bool cropUselessNode(Node* node) {
        visited.push_back(node->id);
        if (!node->removable) return false;
        // The production crop calls removeNode, which deletes this node
        // and erases its entry from m_nodes before returning true.
        m_nodes.erase(std::find(m_nodes.begin(), m_nodes.end(), node));
        delete node;
        return true;
    }
    void sweep() {
'''
        harness += block + r'''
    }
};
int main() {
    Map mixed{{new Node{1,true}, new Node{2,false}, new Node{3,true}, new Node{4,false}}, {}};
    mixed.sweep();
    assert((mixed.visited == std::vector<int>{1,2,3,4}));
    assert(mixed.m_nodes.size() == 2);
    assert(mixed.m_nodes[0]->id == 2 && mixed.m_nodes[1]->id == 4);
    Map all{{new Node{1,true},new Node{2,true}}, {}};
    all.sweep();
    assert(all.m_nodes.empty() && all.visited.size() == 2);
    Map none{{new Node{1,false},new Node{2,false}}, {}};
    none.sweep();
    assert(none.m_nodes.size() == 2 && none.visited.size() == 2);
    Map empty;
    empty.sweep();
    assert(empty.visited.empty());
}
'''
        with tempfile.TemporaryDirectory(prefix="mapping-prune-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-D_GLIBCXX_DEBUG", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
