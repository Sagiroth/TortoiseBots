#!/usr/bin/env python3
"""Fail when a key is read into PlayerbotAIConfig and never consumed.

A dead key misleads administrators: an active config line looks like it
controls something and a commented default documents behaviour that does not
exist. tools/verify_config_defaults.py guards the value of every read; this
guards the consumption of every read.

Scope: every statically visible

    <member> = config.Get(Int|Bool|Float|String)Default("AiPlayerbot.X", ...)

in the module source (PlayerbotAIConfig.cpp and the dungeon-run key re-read),
from wherever the read appears. The member identifier is then looked up across
the module source; it is consumed when any engine file mentions it. Keys whose
member surfaces only in the observability emitter and the dashboard are
reported separately, as `display-only`: those describe dead knobs on the
dashboard too, so they need a decision (wire up or remove) rather than a pass.

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

READ = re.compile(
    r'(\w+)\s*=\s*(?:\(\s*\w+\s*\)\s*)?config\.Get(?:Int|Bool|Float|String)Default\('
    r'\s*"(AiPlayerbot\.[A-Za-z0-9_.]+)"\s*,')

CODE_SUFFIXES = {'.cpp', '.h', '.hpp', '.c', '.cc'}
SCAN_DIR_SKIPS = {'.git', 'build', 'data', 'docs', 'conf', 'scripts', 'tools'}
CONFIG_FILES = ("ai/playerbot/PlayerbotAIConfig.cpp",
                "ai/playerbot/PlayerbotAIConfig.h")
CONFIG_CPP = CONFIG_FILES[0]
DISPLAY_FILES = ("runtime/ObservabilityEmitter.cpp",
                 "tools/observability/web/static/js/app.js")

# Members read into PlayerbotAIConfig that no engine code consumes yet.
# One line of player words why reading it is still the better choice.
ALLOWLIST = {}

DISPLAY_ONLY_ALLOWLIST = {}


def iter_source_files(root):
    for path in sorted(root.rglob('*')):
        if path.is_dir() or path.suffix not in CODE_SUFFIXES:
            continue
        rel = path.relative_to(root).as_posix()
        if rel.startswith('.git/'):
            continue
        yield path, rel


def load_engine_files(root):
    """Return [(rel, lines)] for every C/C++ source outside the excluded dirs."""
    files = []
    for path, rel in iter_source_files(root):
        if rel.split('/')[0] in SCAN_DIR_SKIPS:
            continue
        files.append((rel, path.read_text(errors='replace').splitlines()))
    return files


def scan_reads(root):
    """Return {member: {key: ...}} over every AiPlayerbot Get*Default read."""
    keys_by_member = {}
    for path, rel in iter_source_files(root):
        if rel.endswith('/tools') or any(
                part in SCAN_DIR_SKIPS for part in rel.split('/')[:-1]):
            continue
        text = path.read_text(errors='replace')
        for m in READ.finditer(text):
            keys_by_member.setdefault(m.group(1), set()).add(m.group(2))
    return keys_by_member


def check(root=None):
    """Run the check; return a list of human-readable findings."""
    root = pathlib.Path(root) if root else ROOT
    findings = []
    keys_by_member = scan_reads(root)
    files = load_engine_files(root)
    for member in sorted(keys_by_member):
        if member in ALLOWLIST:
            continue
        needle = re.compile(r'\b%s\b' % re.escape(member))
        engine, display = [], []
        for rel, lines in files:
            if rel in CONFIG_FILES:
                if rel != CONFIG_CPP:
                    continue  # declarations are not consumption
            if rel == CONFIG_CPP:
                # The config file may legitimately consume a member it read
                # (post-processing, facade wiring). Every read line starts
                # with the member's own write, so mask each Get*Default read
                # and count only the remaining mentions.
                text = '\n'.join(lines)
                masked = text
                for m in READ.finditer(text):
                    masked = masked.replace(m.group(0), ' ' * len(m.group(0)))
                lines = masked.split('\n')
            for line_no, line in enumerate(lines, 1):
                if needle.search(line):
                    bucket = display if (rel in DISPLAY_FILES and
                                         line.lstrip().startswith('<<')) else engine
                    bucket.append(f"{rel}:{line_no}")
        if engine:
            continue
        shown = ', '.join(sorted(keys_by_member[member]))
        if member in DISPLAY_ONLY_ALLOWLIST:
            continue
        if display:
            findings.append(
                f"{member}: {shown}: read into PlayerbotAIConfig but only shown on "
                "the dashboard snapshot; wire it up or remove the key")
        else:
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
