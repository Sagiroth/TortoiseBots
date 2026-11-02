#!/usr/bin/env python3
"""Generate runtime/DecisionCaptureFields.gen.h from the run leader's policy structs.

The decision capture writes whole `Facts`/`Run` snapshots; the codec in
runtime/DecisionCaptureCodec.h needs a field list per struct, the schema text
that identifies a capture's format, and one sample value per field (the codec
test round-trips them). Hand lists drift, so this script derives all three from
the structs themselves:

    python3 tools/gen_decision_capture.py            # write the header
    python3 tools/gen_decision_capture.py --check    # exit 1 with a diff when stale

The parser is strict on purpose: inside a struct body every statement must be a
field, a member function, a nested struct/enum, or one of the skipped forms
(using/typedef/static/friend/access specifier). Anything else stops with
`file:line: unrecognised statement`, and a field type outside the allowed list
stops naming the field and the type, so a new kind of member is a decision, not
a silent omission.

Schema naming: namespaces are stripped and enclosing structs kept (`Facts`,
`Run::Straggle`, `Phase`). The text is sorted by the full C++ qualified name,
fields are in declaration order.

The schema text and hash are the capture/fixture format (record version `v` is
separate); a change here means old captures and fixtures need
`replay_decisions upgrade`.
"""

import argparse
import difflib
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
OUTPUT = ROOT / "runtime" / "DecisionCaptureFields.gen.h"

# Captured roots. Everything reachable from their field types is generated too.
ROOTS = (
    "TortoiseBots::DungeonRun::Facts",
    "TortoiseBots::DungeonRun::Run",
    "ai::DungeonPlan",
    "ai::BossTacticRow",
    "TortoiseBots::DungeonRun::RestLines",
)

# The headers scanned, in the order the compiler sees them. Only repository
# headers; the standard library is never parsed.
HEADERS = (
    "runtime/DungeonRunPolicy.h",
    "runtime/GossipOrderPolicy.h",
    "runtime/RemoteCommanderTasks.h",
    "runtime/RaidLayoutPolicy.h",
    "runtime/RezHoldPolicy.h",
    "ai/playerbot/CombatSpreadPolicy.h",
    "ai/playerbot/DungeonPlanPolicy.h",
    "ai/playerbot/BossTacticsPolicy.h",
    "ai/playerbot/policy/combat/RangedApproachPolicy.h",
)

SCALARS = {
    "bool", "char", "int", "unsigned", "longlong",
    "std::int8_t", "std::int16_t", "std::int32_t", "std::int64_t",
    "std::uint8_t", "std::uint16_t", "std::uint32_t", "std::uint64_t",
    "std::size_t", "float", "double", "std::string",
}

SCALAR_DISPLAY = dict((name.replace(" ", ""), name) for name in SCALARS)
SCALAR_DISPLAY["longlong"] = "long long"

CONTAINERS = ("std::vector", "std::deque", "std::set", "std::unordered_set", "std::optional")
MAPS = ("std::map", "std::unordered_map")
INTEGRAL_SCALARS = {
    "char", "int", "unsigned", "long long",
    "std::int8_t", "std::int16_t", "std::int32_t", "std::int64_t",
    "std::uint8_t", "std::uint16_t", "std::uint32_t", "std::uint64_t",
    "std::size_t",
}
FLOAT_SCALARS = {"float", "double"}
SKIP_PREFIXES = ("using ", "typedef ", "static ", "friend ", "template ", "explicit ", "virtual ")


class GenError(Exception):
    def __init__(self, path, line, message):
        super().__init__("%s:%d: %s" % (path, line, message))


class Field:
    def __init__(self, name, node, type_text):
        self.name = name
        self.node = node
        self.type_text = type_text


class Struct:
    def __init__(self, name, qualified, scope):
        self.name = name
        self.qualified = qualified
        self.scope = scope  # tuple of parts
        self.fields = []


class Enum:
    def __init__(self, name, qualified, scope, enumerators):
        self.name = name
        self.qualified = qualified
        self.scope = scope
        self.enumerators = enumerators


class Model:
    def __init__(self):
        self.structs = {}
        self.enums = {}
        self.aliases = {}  # (scope tuple, name) -> qualified

    def add(self, entry):
        table = self.structs if isinstance(entry, Struct) else self.enums
        if entry.qualified in table:
            raise GenError("<model>", 0, "duplicate type %s" % entry.qualified)
        table[entry.qualified] = entry


def fnv1a64(data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def json_text(value):
    """Escape a string the way the C++ codec's JsonString does."""
    out = ['"']
    for byte in value.encode("utf-8"):
        if byte == 0x22:
            out.append('\\"')
        elif byte == 0x5C:
            out.append("\\\\")
        elif byte == 0x08:
            out.append("\\b")
        elif byte == 0x0C:
            out.append("\\f")
        elif byte == 0x0A:
            out.append("\\n")
        elif byte == 0x0D:
            out.append("\\r")
        elif byte == 0x09:
            out.append("\\t")
        elif byte < 0x20:
            out.append("\\u%04x" % byte)
        else:
            out.append(chr(byte))
    out.append('"')
    return "".join(out)


def strip_comments_and_strings(text):
    """Blank comments and literal contents, keeping offsets and line breaks."""
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n:
                if text[i] == "*" and i + 1 < n and text[i + 1] == "/":
                    out[i] = out[i + 1] = " "
                    i += 2
                    break
                if text[i] != "\n":
                    out[i] = " "
                i += 1
        elif c == '"':
            i += 1
            while i < n:
                if text[i] == "\\":
                    out[i] = " "
                    if i + 1 < n and text[i + 1] != "\n":
                        out[i + 1] = " "
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                if text[i] != "\n":
                    out[i] = " "
                i += 1
        elif c == "'":
            i += 1
            while i < n:
                if text[i] == "\\":
                    out[i] = " "
                    if i + 1 < n:
                        out[i + 1] = " "
                    i += 2
                    continue
                if text[i] == "'":
                    i += 1
                    break
                if text[i] != "\n":
                    out[i] = " "
                i += 1
        else:
            i += 1
    return "".join(out)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


class Parser:
    def __init__(self, text, path, model):
        self.text = text
        self.path = path
        self.model = model
        self.pos = 0

    # -- low level -------------------------------------------------------

    def skip_ws(self):
        text = self.text
        n = len(text)
        while self.pos < n and text[self.pos].isspace():
            self.pos += 1

    def error(self, pos, message):
        raise GenError(self.path, line_of(self.text, pos), message)

    def snippet(self, pos, limit=60):
        piece = " ".join(self.text[pos:pos + limit].split())
        return piece or "<end of file>"

    def skip_line(self):
        while self.pos < len(self.text) and self.text[self.pos] != "\n":
            self.pos += 1

    def skip_to(self, stop, track_paren=False):
        """Advance past a top-level `stop` character (not consumed)."""
        paren = 0
        while self.pos < len(self.text):
            c = self.text[self.pos]
            if track_paren:
                if c == "(":
                    paren += 1
                elif c == ")":
                    paren -= 1
            if c == stop and (not track_paren or paren <= 0):
                return
            self.pos += 1
        self.error(self.pos, "unterminated statement before '%s'" % stop)

    def skip_balanced(self, opener, closer):
        assert self.text[self.pos] == opener
        depth = 0
        while self.pos < len(self.text):
            c = self.text[self.pos]
            if c == opener:
                depth += 1
            elif c == closer:
                depth -= 1
                if depth == 0:
                    self.pos += 1
                    return
            self.pos += 1
        self.error(self.pos, "unbalanced '%s'" % opener)

    def read_identifier(self):
        m = re.match(r"[A-Za-z_][A-Za-z_0-9]*", self.text[self.pos:])
        if not m:
            return None
        self.pos += m.end()
        return m.group(0)

    # -- scopes ----------------------------------------------------------

    def parse(self):
        self.parse_scope(())
        if self.pos < len(self.text) and self.text[self.pos:].strip():
            self.error(self.pos, "unrecognised statement at file scope")

    def parse_scope(self, scope):
        while True:
            self.skip_ws()
            if self.pos >= len(self.text):
                return
            c = self.text[self.pos]
            if c == "}":
                self.pos += 1
                return
            if c == ";":
                self.pos += 1
                continue
            if c == "#":
                self.skip_line()
                continue
            word = self.read_identifier()
            if word is None:
                self.error(self.pos, "unrecognised statement at namespace scope: %s" % self.snippet(self.pos))
            self.skip_ws()
            if word == "namespace":
                self.parse_namespace(scope)
            elif word in ("struct", "class", "union"):
                self.parse_struct(word, scope)
            elif word == "enum":
                self.parse_enum(scope)
            elif word == "using":
                self.parse_using(scope)
            else:
                self.skip_scope_statement()

    def parse_namespace(self, scope):
        parts = []
        while True:
            self.skip_ws()
            name = self.read_identifier()
            if name is None:
                self.error(self.pos, "namespaceless namespace")
            parts.append(name)
            self.skip_ws()
            if self.text[self.pos] == ":" and self.text[self.pos + 1] == ":":
                self.pos += 2
                continue
            break
        self.skip_ws()
        if self.text[self.pos] != "{":
            self.error(self.pos, "namespace without a body")
        self.pos += 1
        self.parse_scope(scope + tuple(parts))

    def parse_using(self, scope):
        start = self.pos
        self.skip_to(";")
        body = " ".join(self.text[start:self.pos].split())
        self.pos += 1
        m = re.match(r"using\s+([A-Za-z_][\w:]*)::([A-Za-z_]\w*)\s*$", body)
        if m:
            self.model.aliases[(scope, m.group(2))] = m.group(1) + "::" + m.group(2)

    def skip_scope_statement(self):
        """Skip a function, variable or other declaration at namespace scope."""
        while self.pos < len(self.text):
            c = self.text[self.pos]
            if c == ";":
                self.pos += 1
                return
            if c == "{":
                self.skip_balanced("{", "}")
                return
            if c == "(":
                self.skip_balanced("(", ")")
                continue
            if c == "[":
                self.skip_balanced("[", "]")
                continue
            if c == "}":
                return
            self.pos += 1

    # -- types -----------------------------------------------------------

    def parse_struct(self, kind, scope):
        self.skip_ws()
        name = self.read_identifier()
        if name is None:
            self.error(self.pos, "%s without a name" % kind)
        # Skip a base clause or `final` up to the body or the semicolon.
        self.skip_ws()
        while self.pos < len(self.text) and self.text[self.pos] not in "{;":
            if self.text[self.pos] == "(":
                self.skip_balanced("(", ")")
                continue
            self.pos += 1
        if self.pos >= len(self.text):
            self.error(self.pos, "unterminated %s %s" % (kind, name))
        if self.text[self.pos] == ";":
            self.pos += 1
            return  # forward declaration
        struct = Struct(name, "::".join(scope + (name,)), scope + (name,))
        self.model.add(struct)
        self.pos += 1  # '{'
        self.parse_struct_body(struct)
        self.skip_ws()
        if self.pos < len(self.text) and self.text[self.pos] == ";":
            self.pos += 1

    def parse_struct_body(self, struct):
        while True:
            self.skip_ws()
            if self.pos >= len(self.text):
                self.error(self.pos, "unterminated struct %s" % struct.qualified)
            c = self.text[self.pos]
            if c == "}":
                self.pos += 1
                return
            if c == ";":
                self.pos += 1
                continue
            start = self.pos
            if c == "#":
                self.error(self.pos, "unrecognised statement: %s" % self.snippet(self.pos))
            header, end = self.read_member_header()
            head = " ".join(header.split())

            if end == "brace":
                if head.startswith(("struct ", "class ", "union ")):
                    self.pos = start
                    word = self.read_identifier()
                    self.skip_ws()
                    self.parse_struct(word, struct.scope)
                    continue
                if head.startswith("enum"):
                    self.pos = start
                    self.skip_ws()
                    self.read_identifier()
                    self.skip_ws()
                    self.parse_enum(struct.scope)
                    continue
                first_paren = self.top_level_index(header, "(")
                first_assign = self.top_level_index(header, "=")
                if first_assign < 0 and first_paren >= 0 and self.text[self.pos] == "{":
                    # Function with a body (or a constructor's).
                    self.skip_balanced("{", "}")
                    continue
                # Brace initialiser: consume the braces and the rest of the statement.
                self.skip_balanced("{", "}")
                self.skip_ws()
                while self.pos < len(self.text) and self.text[self.pos] not in ";}":
                    self.pos += 1
                if self.pos < len(self.text) and self.text[self.pos] == ";":
                    self.pos += 1
                else:
                    self.error(self.pos, "unterminated field in %s" % struct.qualified)
                self.parse_field_statement(struct, header, start)
                continue

            # end == "semicolon"
            if not head:
                continue
            if head in ("public:", "private:", "protected:"):
                continue
            if head.startswith(SKIP_PREFIXES):
                continue
            if head.startswith("enum"):
                self.pos = start
                self.skip_ws()
                self.read_identifier()
                self.skip_ws()
                self.parse_enum(struct.scope)
                continue
            if head.startswith(("struct ", "class ", "union ")):
                continue  # a forward declaration
            first_paren = self.top_level_index(header, "(")
            first_assign = self.top_level_index(header, "=")
            if first_paren >= 0 and (first_assign < 0 or first_paren < first_assign):
                continue  # member function declaration, operator, constructor
            if "operator" in head and "(" in head:
                continue
            self.parse_field_statement(struct, header, start)

    def read_member_header(self):
        """Read up to a top-level `;` or `{`, without consuming it."""
        start = self.pos
        paren = 0
        bracket = 0
        angle = 0
        while self.pos < len(self.text):
            c = self.text[self.pos]
            if c == "(":
                paren += 1
            elif c == ")":
                paren -= 1
            elif c == "[":
                bracket += 1
            elif c == "]":
                bracket -= 1
            elif c == "<" and paren == 0 and bracket == 0:
                angle += 1
            elif c == ">" and paren == 0 and bracket == 0:
                angle = max(0, angle - 1)
            elif paren == 0 and bracket == 0:
                if c == ";":
                    header = self.text[start:self.pos]
                    self.pos += 1
                    return header, "semicolon"
                if c == "{":
                    return self.text[start:self.pos], "brace"
                if c == "}":
                    print(self.text)
                    self.error(self.pos, "unexpected '}' in %s" % self.snippet(start))
            self.pos += 1
        self.error(start, "unterminated struct body")

    @staticmethod
    def top_level_index(text, needle):
        paren = 0
        bracket = 0
        angle = 0
        for i, c in enumerate(text):
            if c == "(":
                if needle == "=" or (paren == 0 and bracket == 0 and angle == 0):
                    if needle == "(":
                        return i
                paren += 1
            elif c == ")":
                paren -= 1
            elif c == "[":
                bracket += 1
            elif c == "]":
                bracket -= 1
            elif c == "<" and paren == 0 and bracket == 0:
                angle += 1
            elif c == ">" and paren == 0 and bracket == 0:
                angle = max(0, angle - 1)
            elif c == "=" and needle == "=" and paren == 0 and bracket == 0 and angle == 0:
                return i
        return -1

    # -- enums -----------------------------------------------------------

    def parse_enum(self, scope):
        # `self.pos` is just past the `enum` keyword.
        self.skip_ws()
        if self.text.startswith("class", self.pos) or self.text.startswith("struct", self.pos):
            self.pos += 5
            self.skip_ws()
        name = self.read_identifier()
        if name is None:
            self.error(self.pos, "enum without a name")
        self.skip_ws()
        if self.pos < len(self.text) and self.text[self.pos] == ":":
            self.pos += 1
            self.skip_to("{")
        self.skip_ws()
        if self.pos >= len(self.text) or self.text[self.pos] != "{":
            self.error(self.pos, "enum %s without a body" % name)
        self.pos += 1
        body_start = self.pos
        depth = 1
        while depth:
            if self.pos >= len(self.text):
                self.error(body_start, "unterminated enum %s" % name)
            c = self.text[self.pos]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    break
            self.pos += 1
        body = self.text[body_start:self.pos]
        self.pos += 1
        enumerators = []
        for part in split_top_level(body, ","):
            part = part.strip()
            if not part:
                continue
            m = re.match(r"([A-Za-z_]\w*)", part)
            if not m:
                self.error(body_start, "unrecognised enum entry %s" % part)
            if "(" in part:
                self.error(body_start, "enum entry with an initialiser expression: %s" % part)
            enumerators.append(m.group(1))
        if not enumerators:
            self.error(body_start, "enum %s with no enumerators" % name)
        self.model.add(Enum(name, "::".join(scope + (name,)), scope + (name,), enumerators))
        self.skip_ws()
        if self.pos < len(self.text) and self.text[self.pos] == ";":
            self.pos += 1

    # -- fields ----------------------------------------------------------

    def parse_field_statement(self, struct, header, start):
        parts = split_top_level(header, ",")
        base_type = None
        for index, part in enumerate(parts):
            text = part.strip()
            if not text:
                continue
            text = remove_initializer(text)
            name, type_part, postfix = split_declarator(text)
            if name is None:
                self.error(start, "unrecognised statement: %s" % self.snippet(start))
            if index == 0:
                base_type = type_part
            elif type_part:
                self.error(start, "unrecognised statement: %s" % self.snippet(start))
            if base_type is None or not base_type:
                self.error(start, "field %s without a type" % name)
            node = self.build_field_type(base_type, postfix, struct, name)
            struct.fields.append(Field(name, node, compact_type(node)))

    def build_field_type(self, type_text, postfix, struct, name):
        """Classify one field's type. Unsupported shapes become ("raw", ...) and
        only fail when a captured struct reaches them (resolve_type)."""
        compact = re.sub(r"\s+", "", type_text)
        if compact.endswith("*") or "(" in compact or "&" in compact:
            if postfix.strip():
                return ("raw", compact + postfix.strip())
            return ("ptr", compact, name)
        node = parse_type_node(compact)
        if postfix.strip():
            m = re.fullmatch(r"\[\s*([0-9]+)\s*\]", postfix.strip())
            if not m:
                return ("raw", compact + postfix.strip())
            node = ("c_array", node, int(m.group(1)))
        return node


def split_top_level(text, sep):
    parts = []
    start = 0
    paren = bracket = angle = brace = 0
    for i, c in enumerate(text):
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif c == "[":
            bracket += 1
        elif c == "]":
            bracket -= 1
        elif c == "{":
            brace += 1
        elif c == "}":
            brace -= 1
        elif c == "<" and paren == 0 and bracket == 0 and brace == 0:
            angle += 1
        elif c == ">" and paren == 0 and bracket == 0 and brace == 0:
            angle = max(0, angle - 1)
        elif c == sep and paren == 0 and bracket == 0 and brace == 0 and angle == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return parts


def remove_initializer(text):
    """Drop `= ...` (or a trailing `=`) from one declarator."""
    paren = bracket = 0
    for i, c in enumerate(text):
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif c == "[":
            bracket += 1
        elif c == "]":
            bracket -= 1
        elif c == "=" and paren == 0 and bracket == 0:
            if i + 1 < len(text) and text[i + 1] == "=":
                continue
            return text[:i]
    return text


def split_declarator(text):
    """`const DungeonPlan* plan` -> ('plan', 'const DungeonPlan*', '').

    `std::uint64_t raidIcons[8]` -> ('raidIcons', 'std::uint64_t', '[8]').
    A continuation declarator (no type of its own) gives type_part ''.
    """
    tokens = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if c == ":" and i + 1 < n and text[i + 1] == ":":
            tokens.append(("::", "::", i))
            i += 2
            continue
        m = re.match(r"[A-Za-z_][A-Za-z_0-9]*", text[i:])
        if m:
            tokens.append(("id", m.group(0), i))
            i += m.end()
            continue
        tokens.append(("p", c, i))
        i += 1
    angle = 0
    bracket = 0
    last_id = None
    for kind, value, offset in tokens:
        if kind == "p" and value == "<":
            angle += 1
        elif kind == "p" and value == ">":
            angle = max(0, angle - 1)
        elif kind == "p" and value == "[":
            if angle == 0:
                bracket += 1
        elif kind == "p" and value == "]":
            if angle == 0:
                bracket = max(0, bracket - 1)
        elif kind == "id" and angle == 0 and bracket == 0:
            last_id = (value, offset)
    if last_id is None:
        return None, None, None
    name, offset = last_id
    head = text[:offset]
    postfix = text[offset + len(name):]
    return name, head, postfix


def parse_type_node(compact):
    if compact in SCALARS:
        return ("scalar", SCALAR_DISPLAY[compact])
    for prefix in CONTAINERS:
        opening = prefix + "<"
        if compact.startswith(opening) and compact.endswith(">"):
            inner = compact[len(opening):-1]
            if prefix == "std::optional":
                return ("optional", parse_type_node(inner))
            return (prefix[len("std::"):], parse_type_node(inner))
    if compact.startswith("std::array<") and compact.endswith(">"):
        inner = compact[len("std::array<"):-1]
        parts = split_top_level(inner, ",")
        if len(parts) != 2 or not parts[1].strip().isdigit():
            return ("raw", compact)
        return ("array", parse_type_node(parts[0].strip()), int(parts[1]))
    for prefix in MAPS:
        opening = prefix + "<"
        if compact.startswith(opening) and compact.endswith(">"):
            inner = compact[len(opening):-1]
            parts = split_top_level(inner, ",")
            if len(parts) != 2:
                return ("raw", compact)
            return (prefix[len("std::"):],
                    parse_type_node(parts[0].strip()),
                    parse_type_node(parts[1].strip()))
    if compact.startswith("std::pair<") and compact.endswith(">"):
        inner = compact[len("std::pair<"):-1]
        parts = split_top_level(inner, ",")
        if len(parts) != 2:
            return ("raw", compact)
        return ("pair",
                parse_type_node(parts[0].strip()),
                parse_type_node(parts[1].strip()))
    if re.fullmatch(r"[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*", compact):
        return ("named", compact)
    return ("raw", compact)


def render_type(node):
    kind = node[0]
    if kind == "scalar":
        return node[1]
    if kind == "c_array":
        return "%s[%d]" % (render_type(node[1]), node[2])
    if kind == "array":
        return "std::array<%s, %d>" % (render_type(node[1]), node[2])
    if kind in ("vector", "deque", "set", "unordered_set", "optional"):
        return "std::%s<%s>" % (kind, render_type(node[1]))
    if kind in ("map", "unordered_map"):
        return "std::%s<%s, %s>" % (kind, render_type(node[1]), render_type(node[2]))
    if kind == "pair":
        return "std::pair<%s, %s>" % (render_type(node[1]), render_type(node[2]))
    if kind == "named":
        return node[1]
    if kind == "plan":
        return "const DungeonPlan*"
    if kind == "tactics":
        return "const ai::BossTacticTable*"
    if kind == "ptr":
        return node[1]
    if kind == "raw":
        return node[1]
    if kind in ("struct", "enum"):
        return schema_name(node[1])
    raise AssertionError(kind)


def compact_type(node):
    """A field's type text kept at parse time (resolution renders the final one)."""
    if node[0] in ("ptr", "raw"):
        return node[1]
    return render_type(node)


# -- resolution ----------------------------------------------------------


def schema_name(qualified):
    parts = qualified.split("::")
    # Drop namespaces: a type that is not itself a registered struct/enum
    # scope. The generator knows the enclosing structs from the registry, so
    # nested types keep their path and namespaces disappear.
    return NESTED_NAMES[qualified]


NESTED_NAMES = {}


def collect_nested_names(model):
    """Map each qualified type to its namespace-free, nesting-preserving name."""
    NESTED_NAMES.clear()
    for qualified, entry in list(model.structs.items()) + list(model.enums.items()):
        parts = qualified.split("::")
        kept = [parts[-1]]
        for end in range(len(parts) - 2, 0, -1):
            prefix = "::".join(parts[:end + 1])
            if prefix in model.structs or prefix in model.enums:
                kept.insert(0, parts[end])
            else:
                break
        NESTED_NAMES[qualified] = "::".join(kept)


def resolve_name(model, name, scope):
    base = name.split("::")
    candidates = []
    for length in range(len(scope), -1, -1):
        candidates.append(scope[:length] + tuple(base))
        if len(base) == 1:
            alias = model.aliases.get((scope[:length], base[0]))
            if alias:
                candidates.append(tuple(alias.split("::")))
    for candidate in candidates:
        qualified = "::".join(candidate)
        if qualified in model.structs or qualified in model.enums:
            return qualified
    return None


def resolve_type(model, node, scope, struct, field_name, cache):
    kind = node[0]
    if kind == "scalar":
        return node
    if kind == "c_array":
        return ("c_array", resolve_type(model, node[1], scope, struct, field_name, cache), node[2])
    if kind == "array":
        return ("array", resolve_type(model, node[1], scope, struct, field_name, cache), node[2])
    if kind in ("vector", "deque", "set", "unordered_set", "optional"):
        return (kind, resolve_type(model, node[1], scope, struct, field_name, cache))
    if kind in ("map", "unordered_map"):
        return (kind,
                resolve_type(model, node[1], scope, struct, field_name, cache),
                resolve_type(model, node[2], scope, struct, field_name, cache))
    if kind == "pair":
        return ("pair",
                resolve_type(model, node[1], scope, struct, field_name, cache),
                resolve_type(model, node[2], scope, struct, field_name, cache))
    if kind == "named":
        name = node[1]
        key = (name, scope)
        if key not in cache:
            cache[key] = resolve_name(model, name, scope)
        qualified = cache[key]
        if qualified is None:
            raise GenError("<model>", 0, "unknown type in %s.%s: %s" % (struct.qualified, field_name, name))
        if qualified in model.structs:
            return ("struct", qualified)
        return ("enum", qualified)
    if kind == "ptr":
        compact, name = node[1], node[2]
        base = compact[:-1]
        if base == "constDungeonPlan" and struct.name == "Run" and name == "plan":
            return ("plan",)
        if base == "constai::BossTacticTable" and struct.name == "Run" and name == "liveTactics":
            return ("tactics",)
        raise GenError("<model>", 0, "unsupported pointer in %s.%s: %s (only Run::plan and Run::liveTactics "
                                     "are captured; add a rule to tools/gen_decision_capture.py)"
                       % (struct.qualified, field_name, compact))
    if kind == "raw":
        raise GenError("<model>", 0, "unsupported type in %s.%s: %s (add a rule to "
                                     "tools/gen_decision_capture.py)"
                       % (struct.qualified, field_name, node[1]))
    return node


def captured_graph(model, roots=ROOTS):
    """Resolve the roots' field types and return the reachable structs/enums."""
    cache = {}
    resolved = {}
    collect_nested_names(model)
    queue = list(roots)
    structs = []
    enums = set()
    while queue:
        qualified = queue.pop(0)
        if qualified in resolved:
            continue
        if qualified not in model.structs:
            raise GenError("<model>", 0, "root %s is not a parsed struct" % qualified)
        struct = model.structs[qualified]
        fields = []
        for field in struct.fields:
            node = resolve_type(model, field.node, struct.scope, struct, field.name, cache)
            fields.append(Field(field.name, node, render_type(node)))
            for name in leaf_names(node):
                if name in model.structs and name not in resolved:
                    queue.append(name)
                elif name in model.enums:
                    enums.add(name)
        struct.fields = fields
        resolved[qualified] = (struct, fields)
        structs.append(qualified)
    order = sorted(resolved, key=lambda q: q)
    return [resolved[q] for q in order], sorted(enums, key=lambda q: q), model.enums


def leaf_names(node):
    kind = node[0]
    if kind == "struct" or kind == "enum":
        return [node[1]]
    if kind in ("vector", "deque", "set", "unordered_set", "optional"):
        return leaf_names(node[1])
    if kind in ("c_array", "array"):
        return leaf_names(node[1])
    if kind in ("map", "unordered_map"):
        return leaf_names(node[1]) + leaf_names(node[2])
    if kind == "pair":
        return leaf_names(node[1]) + leaf_names(node[2])
    return []


# -- samples -------------------------------------------------------------


def sample_json(model, node, depth=0):
    kind = node[0]
    if kind == "scalar":
        scalar = node[1]
        if scalar == "bool":
            return "true"
        if scalar in FLOAT_SCALARS:
            return "1.5"
        if scalar == "std::string":
            return '"s"'
        return "7"
    if kind == "enum":
        return "1" if len(model.enums[node[1]].enumerators) >= 2 else "0"
    if kind == "struct":
        if depth >= 3:
            return "{}"
        struct = model.structs[node[1]]
        members = []
        for field in struct.fields:
            if field.node[0] in ("plan", "tactics"):
                continue
            members.append('"%s":%s' % (field.name, sample_json(model, field.node, depth + 1)))
        return "{" + ",".join(members) + "}"
    # The depth cap counts struct nesting, not containers: a container of a
    # struct is still that struct's sample, so the codec test can compare it.
    if kind in ("vector", "deque", "set", "unordered_set", "optional"):
        return "[" + sample_json(model, node[1], depth) + "]"
    if kind in ("c_array", "array"):
        return "[" + ",".join(sample_json(model, node[1], depth) for _ in range(node[2])) + "]"
    if kind in ("map", "unordered_map"):
        return "[[" + sample_json(model, node[1], depth) + "," + sample_json(model, node[2], depth) + "]]"
    if kind == "pair":
        return "[" + sample_json(model, node[1], depth) + "," + sample_json(model, node[2], depth) + "]"
    raise AssertionError(kind)


# -- output --------------------------------------------------------------


def mangled(qualified):
    return schema_name(qualified).replace("::", "_")


def schema_text(model, roots=ROOTS):
    structs, enums, enum_table = captured_graph(model, roots)
    lines = []
    for struct, fields in structs:
        lines.append("struct %s" % schema_name(struct.qualified))
        for field in fields:
            lines.append("  %s: %s" % (field.name, field.type_text))
    for qualified in enums:
        enum = enum_table[qualified]
        lines.append("enum %s: %s" % (schema_name(qualified), ",".join(enum.enumerators)))
    return "\n".join(lines) + "\n"


def generate_header(model, roots=ROOTS):
    collect_nested_names(model)
    structs, enums, enum_table = captured_graph(model, roots)
    names = {}
    for struct, _fields in structs:
        names[mangled(struct.qualified)] = struct.qualified
    for qualified in enums:
        names[mangled(qualified)] = qualified
    seen = {}
    for mangle, qualified in names.items():
        if mangle in seen and seen[mangle] != qualified:
            raise GenError("<model>", 0, "schema name clash: %s and %s both mangle to %s"
                           % (seen[mangle], qualified, mangle))
        seen[mangle] = qualified

    text = schema_text(model, roots)
    digest = "%016x" % fnv1a64(text)

    out = []
    out.append("// Generated by tools/gen_decision_capture.py; do not edit.")
    out.append("//")
    out.append("// X-macro field lists (declaration order), the schema text and hash of the")
    out.append("// captured structs, and one sample value per field for the codec test.")
    out.append("#pragma once")
    out.append("")
    out.append("namespace TortoiseBots {")
    out.append("namespace DecisionCapture {")
    out.append("")
    for struct, fields in structs:
        out.append("#define TB_DCAP_FIELDS_%s(X) %s" % (mangled(struct.qualified),
                                                       " ".join("X(%s)" % f.name for f in fields)))
    out.append("")
    out.append("#define TB_DCAP_STRUCTS(S) \\")
    for index, (struct, _fields) in enumerate(structs):
        tail = " \\" if index + 1 < len(structs) else ""
        out.append("    S(%s, %s)%s" % (mangled(struct.qualified), struct.qualified, tail))
    out.append("")
    out.append("inline constexpr char kDecisionSchemaText[] = R\"TBSCHEMA(" + text + ")TBSCHEMA\";")
    out.append("")
    out.append('inline constexpr char kDecisionSchemaHash[] = "%s";' % digest)
    out.append("")
    out.append("struct DecisionSampleField")
    out.append("{")
    out.append("    const char* field;")
    out.append("    const char* json;")
    out.append("};")
    out.append("")
    for struct, fields in structs:
        samples = []
        for field in fields:
            if field.node[0] in ("plan", "tactics"):
                continue
            samples.append('{"%s", R"TBSAMPLE(%s)TBSAMPLE"}' % (field.name, sample_json(model, field.node)))
        if samples:
            out.append("inline constexpr DecisionSampleField kDecisionSamples_%s[] = {"
                       % mangled(struct.qualified))
            out.append("    " + ", ".join(samples) + ",")
            out.append("    {nullptr, nullptr},")
            out.append("};")
        else:
            out.append("inline constexpr DecisionSampleField kDecisionSamples_%s[] = {"
                       % mangled(struct.qualified))
            out.append("    {nullptr, nullptr},")
            out.append("};")
        out.append("")
    out.append("} // namespace DecisionCapture")
    out.append("} // namespace TortoiseBots")
    out.append("")
    return "\n".join(out)


def scan_source(text, path):
    """Parse one source text into a Model (the repo files or a test snippet)."""
    model = Model()
    Parser(strip_comments_and_strings(text), path, model).parse()
    return model


def load_model():
    model = Model()
    for relative in HEADERS:
        path = ROOT / relative
        if not path.exists():
            raise GenError(relative, 0, "missing header")
        text = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        Parser(text, relative, model).parse()
    return model


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true",
                        help="exit 1 with a diff when runtime/DecisionCaptureFields.gen.h is stale")
    parser.add_argument("--scan-file", metavar="PATH",
                        help="parse one header and resolve its structs, then exit "
                             "(exit 2 on an unrecognised statement or unsupported type)")
    args = parser.parse_args(argv)
    if args.scan_file:
        path = pathlib.Path(args.scan_file)
        try:
            model = scan_source(path.read_text(encoding="utf-8"), str(path))
            captured_graph(model, tuple(model.structs))
        except GenError as error:
            print(error, file=sys.stderr)
            return 2
        return 0
    try:
        model = load_model()
        header = generate_header(model)
    except GenError as error:
        print(error, file=sys.stderr)
        return 2
    relative = OUTPUT.relative_to(ROOT)
    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current == header:
            return 0
        diff = difflib.unified_diff(current.splitlines(), header.splitlines(),
                                    fromfile=str(relative), tofile=str(relative) + " (regenerated)",
                                    lineterm="")
        sys.stdout.write("\n".join(diff) + "\n")
        print("stale: run python3 tools/gen_decision_capture.py", file=sys.stderr)
        return 1
    OUTPUT.write_text(header, encoding="utf-8")
    print("wrote %s (%d bytes)" % (relative, len(header)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
