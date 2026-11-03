"""The guild order row statements all speak one column order.

The load SELECT is built from runtime/GuildOrderStoreColumns.h and read by
those index constants (tools/test_guild_order_columns.cpp pins the two
against each other). The two write statements still spell their columns out,
so this checks they use the same columns in the same order: the REPLACE
writes a whole row, the UPDATE everything but the columns that cannot change
after the row is placed. A duplicated or reordered column here would bind a
value into its neighbour's field even though the load is correct.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
HEADER = (ROOT / "runtime/GuildOrderStoreColumns.h").read_text()

# Columns the UPDATE never touches: set once by the REPLACE (or the key).
IMMUTABLE = {"bot_guid", "guild_id", "type", "item_id", "created_at"}


def select_columns():
    body = HEADER[HEADER.index("kSelect[] = {"):]
    body = body[:body.index("};")]
    return re.findall(r'"([a-z_]+)"', body)


def replace_columns():
    body = STORE.split("REPLACE INTO `tortoise_bots_guild_order` ", 1)[1]
    body = body[:body.index("VALUES")]
    return re.findall(r"`([a-z_]+)`", body)


def update_columns():
    body = STORE[STORE.index("UPDATE `tortoise_bots_guild_order` SET"):]
    body = body[:body.index("WHERE `bot_guid`")]
    return re.findall(r"`([a-z_]+)` = ", body)


class GuildOrderStoreColumnsTest(unittest.TestCase):
    def test_the_load_reads_by_the_header_constants(self):
        self.assertIn("StoreColumns::SelectList()", STORE)
        self.assertIn("fields[StoreColumns::ColCarried].GetUInt32()", STORE)
        self.assertIn("fields[StoreColumns::ColGuildGoldHeld].GetUInt32()", STORE)
        self.assertIn("fields[StoreColumns::ColState].GetCppString()", STORE)

    def test_replace_writes_the_loaded_row_in_the_same_order(self):
        self.assertEqual(replace_columns(), select_columns())

    def test_update_writes_every_mutable_column_in_the_same_order(self):
        expected = [column for column in select_columns() if column not in IMMUTABLE]
        self.assertEqual(update_columns(), expected)

    def test_carried_appears_once_in_every_statement(self):
        self.assertEqual(select_columns().count("carried"), 1)
        self.assertEqual(replace_columns().count("carried"), 1)
        self.assertEqual(update_columns().count("carried"), 1)
        self.assertEqual(select_columns()[-1], "carried")


if __name__ == "__main__":
    unittest.main()
