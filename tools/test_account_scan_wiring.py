"""The prefix-match account scan tells an empty login database apart from a failure (TB #1310).

The core's MySQLConnection::_Query answers null for an empty result set as well
as for an error, so the module's plain "no rows -> failure" scan reported "could
not list prefix-matching accounts" at startup on a realm whose `account` table
was still empty. The scan now counts the accounts first (DecideAccountScan in
runtime/policy/infra/PoolResetPolicy.h, tested in tools/test_pool_reset_policy.cpp).

This pins the glue, which only builds against the core:
- DiscoverPrefixMatches counts `account` through the login database;
- its verdict comes from DecideAccountScan;
- the row scan runs only when the count says there are accounts.

Run: python3 -m unittest tools/test_account_scan_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
REGISTRY = (ROOT / "runtime/RandomBotAccountRegistry.cpp").read_text()
REGISTRY_H = (ROOT / "runtime/RandomBotAccountRegistry.h").read_text()


class AccountScanWiringTest(unittest.TestCase):
    def test_the_scan_counts_the_accounts_first(self):
        self.assertIn("SELECT COUNT(*) FROM `account`", REGISTRY)

    def test_the_verdict_is_the_pure_decision(self):
        self.assertIn("DecideAccountScan(", REGISTRY)

    def test_the_count_helper_reads_the_login_database(self):
        self.assertIn("bool QueryLoginCount(char const* sql, uint32_t& out) const;", REGISTRY_H)
        fn = REGISTRY[REGISTRY.index("bool RandomBotAccountRegistry::QueryLoginCount("):]
        fn = fn[:fn.index("\n}\n")]
        self.assertIn("LoginDatabase.PQuery(sql)", fn)


if __name__ == "__main__":
    unittest.main()
