"""The guild order REPLACE binds one value to each column.

A full-row write is a REPLACE whose column list, VALUES list and bound
argument list must line up position for position. The carry column was added
with an extra '%u' at the head of the value list instead of a placeholder at
its end, so from `supply_step` on every value sat one column early: a step
token went into a '%u', an item id into a '%s'. The three counts still
matched, so counting alone would not have caught it; this test checks each
column's placeholder and the argument behind it.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()

# How each column's value is spelled in the statement, written out here so
# the statement is checked against a second copy rather than only itself.
# u = '%u', s = '%s', t = FROM_UNIXTIME('%u').
EXPECTED = [
    ("bot_guid", "u"),
    ("guild_id", "u"),
    ("giver_guid", "u"),
    ("parent_bot", "u"),
    ("root_giver", "u"),
    ("type", "s"),
    ("item_id", "u"),
    ("amount", "u"),
    ("hours", "u"),
    ("delivered", "u"),
    ("state", "s"),
    ("reason", "s"),
    ("farm_zone", "u"),
    ("farm_chance", "u"),
    ("farm_item", "u"),
    ("zone_index", "u"),
    ("deaths_in_zone", "u"),
    ("worked_sec", "u"),
    ("worked_total", "u"),
    ("worked_nogain", "u"),
    ("reported", "u"),
    ("farm_rest", "u"),
    ("keep", "u"),
    ("gold_cap", "u"),
    ("gold_cap_set", "u"),
    ("guild_gold_withdrawn", "u"),
    ("guild_gold_held", "u"),
    ("cancelled", "u"),
    ("supply_step", "s"),
    ("supply_return_step", "s"),
    ("supply_item", "u"),
    ("supply_count", "u"),
    ("supply_gold_count", "u"),
    ("need_items", "s"),
    ("need_whispered", "u"),
    ("last_need_whisper", "s"),
    ("need_bank_denied", "u"),
    ("stock_target", "u"),
    ("last_bank_check", "u"),
    ("created_at", "t"),
    ("updated_at", "t"),
    ("carried", "u"),
    ("bring", "s"),
    ("given", "s"),
]

VALUE = {"u": "'%u'", "s": "'%s'", "t": "FROM_UNIXTIME('%u')"}
STRING_ARG = re.compile(r"Token\(|\.c_str\(\)")


def _balanced(text, start):
    """Text inside the parens opening at `start`, quotes respected."""
    depth = 1
    quote = None
    i = start
    while i < len(text):
        ch = text[i]
        if quote:
            if ch == "\\":
                i += 1
            elif ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return text[start:i]
        i += 1
    raise AssertionError("unbalanced parens")


def _split_top(text):
    out = []
    depth = 0
    current = ""
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(current.strip())
            current = ""
        else:
            current += ch
    out.append(current.strip())
    return out


def replace_statement():
    anchor = STORE.index("REPLACE INTO `tortoise_bots_guild_order`")
    start = STORE.rindex("CharacterDatabase.DirectPExecute(", 0, anchor)
    open_paren = start + len("CharacterDatabase.DirectPExecute")
    return _balanced(STORE, open_paren + 1)


def replace_parts():
    call = replace_statement()
    literals = list(re.finditer(r'"((?:[^"\\]|\\.)*)"', call))
    first_value = next(
        i for i, m in enumerate(literals) if m.group(1).lstrip().startswith("VALUES")
    )
    value_literals = literals[first_value:]
    fmt = "".join(m.group(1) for m in value_literals)
    columns = [
        name
        for name in re.findall(r"`([a-z_]+)`", call[: value_literals[0].start()])
        if name != "tortoise_bots_guild_order"
    ]
    values = fmt[len("VALUES"):].strip()
    values = values[1:-1].strip()
    args = call[value_literals[-1].end():].strip()
    assert args.startswith(","), args[:40]
    args = args[1:].strip()
    return columns, values, fmt, _split_top(args)


class GuildOrderReplaceStatementTest(unittest.TestCase):
    def test_column_value_and_argument_counts_match(self):
        columns, values, fmt, args = replace_parts()
        expressions = _split_top(values)
        self.assertEqual(len(columns), len(EXPECTED))
        self.assertEqual(len(columns), len(expressions), "one VALUES entry per column")
        self.assertEqual(len(columns), fmt.count("%"), "one placeholder per column")
        self.assertEqual(len(columns), len(args), "one bound argument per placeholder")

    def test_every_column_binds_its_own_value(self):
        columns, values, _fmt, _args = replace_parts()
        expressions = _split_top(values)
        expected = [(column, VALUE[spec]) for column, spec in EXPECTED]
        self.assertEqual(list(zip(columns, expressions)), expected)

    def test_every_argument_matches_its_placeholder(self):
        _columns, _values, _fmt, args = replace_parts()
        for (column, spec), arg in zip(EXPECTED, args):
            with self.subTest(column=column):
                if spec == "s":
                    self.assertRegex(arg, STRING_ARG, "a '%s' needs a string argument")
                else:
                    self.assertNotRegex(arg, STRING_ARG, "a numeric placeholder needs a number")

    def test_bring_binds_the_bring_token_second_to_last(self):
        columns, values, _fmt, args = replace_parts()
        self.assertEqual(columns[-2], "bring")
        self.assertEqual(_split_top(values)[-2], "'%s'")
        self.assertIn("BringItemsToken(record.bringItems)", args[-2])

    def test_given_binds_the_given_token_last(self):
        columns, values, _fmt, args = replace_parts()
        self.assertEqual(columns[-1], "given")
        self.assertEqual(_split_top(values)[-1], "'%s'")
        self.assertIn("GivenItemsToken(record.given)", args[-1])


if __name__ == "__main__":
    unittest.main()
