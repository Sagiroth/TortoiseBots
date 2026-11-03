#!/usr/bin/env python3
"""Fail when a key is read into PlayerbotAIConfig and never consumed.

A dead key misleads administrators: an active config line looks like it
controls something and a commented default documents behaviour that does not
exist. tools/verify_config_defaults.py guards the value of every read; this
guards the consumption of every read.

Scope: one combined scan finds every statically visible
`config.Get*Default("AiPlayerbot.X", ...)` call in the module source
(plain assignments, `static_cast`/C-cast/`std::min`/`std::max` wrappers,
enum and parser constructors, `LoadList`/`LoadListString` container
arguments, direct call arguments). Each read is attributed to the member
that receives the value: the assignment target when it names a declared
config member (locals that the config file parses into a member just below
the read attribute to that member), else the `LoadList` container when it
names one, else nothing (the value is consumed at the call site). A read
nested as another read's default shares the outer read's attribution.
The member identifier is then tested against a token index built from
every engine file in a single pass; it is consumed when any engine file
mentions it, including through a called inline or out-of-line config
accessor (hasLog, IsInRandomQuestItemList). The emitter's Initialize
counts as engine code; only the snapshot stream below it is display, and
a member named only there is reported `display-only`: a dead knob on the
dashboard still needs a decision (wire up or remove) rather than a pass.

Not necessary to consume a key, hence excluded from usage scan:
- ai/playerbot/PlayerbotAIConfig.{cpp,h}: the read itself (the .cpp only after
  masking every read, so post-processing there still counts as use);
- docs/, data/, conf/, scripts/: prose and templates;
- tools/: unit-test harnesses copy the config struct and the dashboard mirrors
  the emitter, so a mention there never means the engine consumes the key.

ALLOWLIST gives a reason to keep for keys read but intentionally not yet
consumed; DISPLAY_ONLY_ALLOWLIST for keys consumed only by the snapshot.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# One combined expression for every read form, so the repo is scanned once
# for reads: a quoted AiPlayerbot key passed to config.Get*Default.
GET = re.compile(
    r'\b\w+\.Get(?:Int|Bool|Float|String)Default\s*\(\s*"(AiPlayerbot\.[A-Za-z0-9_.]+)"')
ASSIGN_TARGET = re.compile(r'^\s*(?:[\w:<>]+\s+)*(\w+)\s*=')
LOADLIST_TAIL = re.compile(
    r'\)\s*,\s*(\w+)\s*\)')
CODE_SUFFIXES = {'.cpp', '.h', '.hpp', '.c', '.cc'}
SCAN_DIR_SKIPS = {'.git', 'build', 'data', 'docs', 'conf', 'scripts', 'tools'}
CONFIG_FILES = ("ai/playerbot/PlayerbotAIConfig.cpp",
                "ai/playerbot/PlayerbotAIConfig.h")
CONFIG_CPP = CONFIG_FILES[0]
DISPLAY_FILES = ("runtime/ObservabilityEmitter.cpp",)

# Members read into PlayerbotAIConfig that no engine code consumes yet.
# One line of player words why reading it is still the better choice.
ALLOWLIST = {
}

DISPLAY_ONLY_ALLOWLIST = {}


def strip_comments(text):
    """Remove // and /* */ comments; string contents are left alone."""
    out = []
    i, n = 0, len(text)
    in_str = None
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == in_str:
                in_str = None
            i += 1
        elif c in '"\'':
            in_str = c
            out.append(c)
            i += 1
        elif c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                i += 1
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            i += 2
            while i + 1 < n and not (text[i] == '*' and text[i + 1] == '/'):
                if text[i] == '\n':
                    out.append('\n')
                i += 1
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def config_members(root):
    """Return the declared PlayerbotAIConfig member names."""
    members = set()
    header = pathlib.Path(root) / 'ai/playerbot/PlayerbotAIConfig.h'
    body = strip_comments(header.read_text(errors='replace'))
    body = body[body.index('class PlayerbotAIConfig'):]
    body = body[:body.rindex('};')]
    body = re.sub(r'\{[^{}]*\}', ' ', body)
    for m in re.finditer(r'[^;()]+?;', body):
        decl = m.group(0)[:-1].strip()
        if not decl or '(' in decl:
            continue  # method declaration, not a member
        chunks = [c.strip() for c in decl.split(',')]
        names = [chunks[0]] + chunks[1:]
        for chunk in names:
            name = chunk.split('=')[0].strip().split()
            if not name:
                continue
            name = re.sub(r'\[.*', '', name[-1]).lstrip('*&')
            if re.fullmatch(r'[A-Za-z_]\w*', name or ''):
                if chunk is chunks[0] and len(chunks[0].split()) < 2:
                    continue
                members.add(name)
    return members
def read_destination(clean, match, members, depth=0):
    """Return the member receiving this Get*Default value, or None.

    None means the value is consumed at the call site: a direct call
    argument with no member stored, or a same-function local (parsed and
    validated a few lines below the read). Attribute from the statement
    start: the assignment target when it is a declared member, else the
    LoadList container argument when it is one, else nothing. A read
    nested as another read's default shares the outer read's attribution.
    """
    if depth > 4:
        return None
    stmt_start = max(clean.rfind(';', 0, match.start()),
                     clean.rfind('{', 0, match.start()),
                     clean.rfind('}', 0, match.start()))
    stmt_start = stmt_start + 1 if stmt_start != -1 else 0
    tail_end = clean.find(';', match.end())
    tail = clean[match.start():tail_end if tail_end != -1 else len(clean)]
    head = clean[stmt_start:match.start()]
    outer = [o for o in GET.finditer(head) if o.end() <= match.start()]
    if outer:
        return read_destination(clean, outer[-1], members, depth + 1)
    load_member = None
    tail_m = LOADLIST_TAIL.search(tail)
    if tail_m and tail_m.group(1) in members:
        load_member = tail_m.group(1)
    m = ASSIGN_TARGET.match(head.split('\n', 1)[-1] if '\n' in head
                            else head)
    if m is None:
        first_line = head.strip().split('\n')[0] if head.strip() else ''
        m = ASSIGN_TARGET.match(first_line)
    if m:
        name = m.group(1)
        if name in members:
            return name
        if name in ('LoadList', 'LoadListString'):
            if load_member is not None:
                return load_member
            # Block-local container (parsed into a member a few lines below):
            # fall through to the local-flow search with no local to discard.
            name = None
        if name is not None:
            # Same-function local (parsed, validated or forwarded just below
            # the read): attribute to the member it flows into, when the
            # next few statements store exactly one config member
            # (assignment, push_back/insert/emplace, or a parse into it).
            window = clean[match.start():match.start() + 1200]
            window = window[:window.find('}') if '}' in window else 1200]
            nxt = GET.search(window, 1)
            if nxt:
                window = window[:nxt.start()]
            stores = {w for w in re.findall(r'(\w+)\s*=', window)
                      if w in members}
            stores |= {w for w in re.findall(
                r'(\w+)\s*\.\s*(?:push_back|insert|emplace(?:_back|_front)?)\s*\(',
                window) if w in members}
            # Parsed straight into a member array (`ParseX(text, member)`):
            # the member is named as a call argument on the next lines; it
            # wins over later unrelated stores in the window.
            parsed = {w for w in re.findall(r'\(\s*\w+\s*,\s*(\w+)\s*\)',
                                            window[:600])
                      if w in members}
            parsed.discard(name)
            if len(parsed) == 1:
                return next(iter(parsed))
            stores.discard(name)
            if len(stores) == 1:
                return next(iter(stores))
            return None  # consumed at the call site (or ambiguous locals)
    if load_member is not None:
        return load_member
    # Bare `LoadList...(Get...(...), local)` with no assignment head:
    # same block-flow search as a local.
    if re.search(r'LoadList\w*\s*<', head):
        block_end = clean.find('}', match.end())
        block = clean[match.start():block_end if block_end != -1
                      else len(clean)]
        block = block[:2000]
        stores = {w for w in re.findall(r'(\w+)\s*=', block) if w in members}
        stores |= {w for w in re.findall(
            r'(\w+)\s*\.\s*(?:push_back|insert|emplace(?:_back|_front)?)\s*\(',
            block) if w in members}
        if tail_m:
            stores.discard(tail_m.group(1))
        if len(stores) == 1:
            return next(iter(stores))
    return None


def iter_code_files(root):
    for path in sorted(root.rglob('*')):
        if path.is_dir() or path.suffix not in CODE_SUFFIXES:
            continue
        rel = path.relative_to(root).as_posix()
        if rel.startswith('.git/'):
            continue
        if rel.split('/')[0] in SCAN_DIR_SKIPS:
            continue
        yield path, rel


def scan_reads(root=None):
    """Return ({member: {keys}}, (call sites, dynamic sites)) over the module."""
    root = pathlib.Path(root) if root else ROOT
    members = config_members(root)
    keys_by_member = {}
    literal_sites = 0
    dynamic_sites = 0
    for path, _rel in iter_code_files(root):
        clean = strip_comments(path.read_text(errors='replace'))
        for m in GET.finditer(clean):
            literal_sites += 1
            member = read_destination(clean, m, members)
            if member is not None:
                keys_by_member.setdefault(member, set()).add(m.group(1))
        for m in re.finditer(
                r'\b\w+\.Get(?:Int|Bool|Float|String)Default\s*\(', clean):
            if not GET.match(clean, m.start()):
                dynamic_sites += 1
    return keys_by_member, (literal_sites, dynamic_sites)


def check(root=None):
    """Run the check; return a list of human-readable findings."""
    root = pathlib.Path(root) if root else ROOT
    findings = []
    keys_by_member, _ = scan_reads(root)
    # Single pass: one token set per file, each file stripped of comments
    # and tokenized once, then every member is tested against the index.
    engine_index = {}  # rel -> token set (None for the config header)
    display_index = {}  # rel -> token set, snapshot stream only
    for path, rel in iter_code_files(root):
        if rel in CONFIG_FILES:
            if rel != CONFIG_CPP:
                continue  # declarations are not consumption
            clean = strip_comments(path.read_text(errors='replace'))
            masked = clean
            for m in GET.finditer(clean):
                start = clean.rfind('\n', 0, m.start()) + 1
                end = clean.find(';', m.end())
                end = end + 1 if end != -1 else len(clean)
                masked = (masked[:start] + ' ' * (end - start)
                          + masked[end:])
            engine_index[rel] = set(re.findall(
                r'\w+', strip_comments(masked)))
            continue
        text = strip_comments(path.read_text(errors='replace'))
        if rel in DISPLAY_FILES:
            # The emitter's Initialize reads the same members to decide
            # what to send; only the snapshot stream below it is display.
            # Split at the snapshot entry point (Snapshot method) when
            # present, else treat the whole file as engine code.
            at = text.find('::Snapshot')
            if at == -1:
                engine_index[rel] = set(re.findall(r'\w+', text))
            else:
                engine_index[rel] = set(re.findall(r'\w+', text[:at]))
                display_index[rel] = set(re.findall(r'\w+', text[at:]))
            continue
        engine_index[rel] = set(re.findall(r'\w+', text))
    # Inline member functions in the config header (hasLog) run as engine
    # code: the members they name are consumed wherever the function is
    # called, so count a header inline body only when the function is
    # actually called outside the config files.
    header = pathlib.Path(root) / 'ai/playerbot/PlayerbotAIConfig.h'
    hclean = strip_comments(header.read_text(errors='replace'))
    bodies = {}
    for m in re.finditer(r'(\w+)\s*\([^;{}]*\)\s*\{([^{}]*)\}', hclean):
        bodies.setdefault(m.group(1), set()).update(
            re.findall(r'\w+', m.group(2)))
    called = set()
    for path, rel in iter_code_files(root):
        if rel in CONFIG_FILES:
            continue
        called.update(re.findall(r'\.(\w+)\s*\(', strip_comments(
            path.read_text(errors='replace'))))
    inline_uses = set()
    for func, uses in bodies.items():
        if func in called:
            inline_uses.update(uses)
    engine_index[str(CONFIG_FILES[1])] = inline_uses
    # Out-of-line accessors in the config cpp (IsInRandomQuestItemList):
    # the members they name are consumed wherever the function is called.
    # Initializers (Initialize/LoadUpdates/ReloadDungeonRunKeys) only hold
    # the reads themselves, so they never count as consumption.
    cpp = strip_comments((pathlib.Path(root) / CONFIG_CPP).read_text(
        errors='replace'))
    for m in re.finditer(
            r'PlayerbotAIConfig::(\w+)\s*\([^;{}]*\)\s*\{([^{}]*)\}', cpp):
        if m.group(1) in called and not GET.search(m.group(2)):
            engine_index[f'{CONFIG_CPP}#{m.group(1)}'] = set(
                re.findall(r'\w+', m.group(2)))
    for member in sorted(keys_by_member):
        if member in ALLOWLIST:
            continue
        if any(member in tokens for tokens in engine_index.values()):
            continue
        shown = ', '.join(sorted(keys_by_member[member]))
        # The observability snapshot streams live values to the dashboard;
        # a member it names is consumed, not dead. Members the snapshot
        # only echoes with no other engine reader stay display-only.
        if any(member in tokens for tokens in display_index.values()):
            if member in DISPLAY_ONLY_ALLOWLIST:
                continue
            findings.append(
                f"{member}: {shown}: read into PlayerbotAIConfig but only shown on "
                "the dashboard snapshot; wire it up or remove the key")
            continue
        findings.append(
            f"{member}: {shown}: read into PlayerbotAIConfig but the member is "
            "used nowhere outside the read; wire it up or remove the key "
            "(old configurations that still set it keep loading: the reader "
            "ignores unknown keys)")
    return findings


def main():
    findings = check()
    for member, reason in sorted({**ALLOWLIST, **DISPLAY_ONLY_ALLOWLIST}.items()):
        print(f"allowlisted {member}: {reason}")
    _, (literal_sites, dynamic_sites) = scan_reads()
    print(f"read scan: {literal_sites} AiPlayerbot Get*Default call sites "
          f"({dynamic_sites} dynamic keys not attributable to a member).")
    if findings:
        print()
        for f in findings:
            print(f"DEAD {f}")
        print(f"\n{len(findings)} config key(s) are read but never used.")
        return 1
    print("\nEvery key read into PlayerbotAIConfig is consumed somewhere.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
