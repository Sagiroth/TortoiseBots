"""Exercise production hire methods against core-shaped group/logout doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class HireDeferredDismissalTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        (directory / 'ObjectGuid.h').write_text('''#pragma once
#include <string>
struct ObjectGuid {
 unsigned value = 0;
 unsigned GetCounter() const { return value; }
 bool IsEmpty() const { return !value; }
 std::string GetString() const { return std::to_string(value); }
 bool operator==(ObjectGuid other) const { return value == other.value; }
 bool operator!=(ObjectGuid other) const { return !(*this == other); }
};
''')
        source = (ROOT / 'runtime/HireLifecycle.cpp').read_text()
        signatures = ['bool HireLifecycle::MasterOnline(',
                      'bool HireLifecycle::MasterLeftGroup(',
                      'void HireLifecycle::Dismiss(',
                      'void HireLifecycle::OnGroupMemberRemoved(',
                      'void HireLifecycle::OnGroupDisband(']
        queued = 'void HireLifecycle::DrainDismissals(' in source
        if queued:
            signatures += ['void HireLifecycle::QueueDismissal(',
                           'void HireLifecycle::DrainDismissals(']
        methods = '\n'.join(function(source, s) for s in signatures)
        if 'Group* HireParty(' in source:
            methods = function(source, 'Group* HireParty(') + '\n' + methods
        harness = (ROOT / 'tools/hire_deferred_dismissal_fixture.inc').read_text()
        harness = '#define HAS_QUEUE ' + str(int(queued)) + '\n' + harness
        (directory / 'fixture.cpp').write_text(harness.replace('// PRODUCTION_METHODS', methods))
        cls.binary = directory / 'fixture'
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-O0', '-g',
                        '-I' + str(directory), '-I' + str(ROOT),
                        str(directory / 'fixture.cpp'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, case):
        subprocess.run([str(self.binary), case], check=True)

    def test_mass_dismissal_callbacks_unwind_before_logout(self):
        self.run_case('mass')

    def test_battleground_entry_and_exit_keep_original_party(self):
        self.run_case('bg')

    def test_pending_intents_revalidate_and_drain_once(self):
        self.run_case('queue')

    def test_world_update_drains_before_deletion(self):
        source = (ROOT / 'runtime/HireLifecycle.cpp').read_text()
        update = function(source, 'void HireLifecycle::Update(')
        self.assertLess(update.index('DrainDismissals();'), update.index('SweepDeletions(now);'))


if __name__ == '__main__':
    unittest.main()
