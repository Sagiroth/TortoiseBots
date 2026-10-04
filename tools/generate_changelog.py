#!/usr/bin/env python3
"""
tools/generate_changelog.py

Automated changelog generator for TortoiseBots.
Pulls merged PRs from GitHub since the last tag or CHANGELOG entry,
uses an OpenCode / OpenAI-compatible model (e.g. DeepSeek) to summarize them
into human-readable gamer-friendly notes, and optionally updates CHANGELOG.md
and outputs release notes for GitHub Releases.

Usage:
  export OPENCODE_API_KEY="sk-..."
  python3 tools/generate_changelog.py [--dry-run] [--write] [--out-notes notes.md]
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import urllib.request
import urllib.error
import uuid

DEFAULT_BASE_URL = os.environ.get("OPENCODE_ENDPOINT") or os.environ.get("OPENCODE_BASE_URL", "")
DEFAULT_MODEL = os.environ.get("OPENCODE_MODEL", "")
CHANGELOG_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "CHANGELOG.md")


def get_last_release_info():
    """Find the date/tag of the latest release or latest section in CHANGELOG.md."""
    # 1. Try to get latest tag via git
    try:
        tag = subprocess.check_output(
            ["git", "describe", "--tags", "--abbrev=0"],
            stderr=subprocess.DEVNULL,
            text=True
        ).strip()
        tag_date = subprocess.check_output(
            ["git", "log", "-1", "--format=%cI", tag],
            stderr=subprocess.DEVNULL,
            text=True
        ).strip()
        return tag, tag_date
    except Exception:
        pass

    # 2. Try to read CHANGELOG.md for the most recent date header (e.g. ## 2026-09-14)
    if os.path.exists(CHANGELOG_PATH):
        with open(CHANGELOG_PATH, "r", encoding="utf-8") as f:
            for line in f:
                m = re.match(r"^##\s+(\d{4}-\d{2}-\d{2})", line)
                if m:
                    # Convert to ISO datetime at start of day
                    date_str = m.group(1)
                    return date_str, f"{date_str}T00:00:00Z"

    return None, None


def get_merged_prs_since(since_iso_date=None):
    """Retrieve merged pull requests using the gh CLI."""
    cmd = [
        "gh", "pr", "list",
        "--state", "merged",
        "--base", "main",
        "--limit", "100",
        "--json", "number,title,body,mergedAt,author,url"
    ]
    try:
        output = subprocess.check_output(cmd, text=True)
        prs = json.loads(output)
    except Exception as e:
        print(f"Error fetching PRs via gh CLI: {e}", file=sys.stderr)
        return []

    if not since_iso_date:
        return prs

    # Filter by mergedAt > since_iso_date
    filtered = []
    for pr in prs:
        merged_at = pr.get("mergedAt")
        if merged_at and merged_at > since_iso_date:
            filtered.append(pr)

    return filtered


def generate_summary_with_ai(prs, api_key, base_url, model):
    """Send PR metadata to OpenCode / OpenAI-compatible endpoint for summarization."""
    pr_summaries = []
    for pr in prs:
        body = (pr.get("body") or "").strip()
        # Truncate long bodies to avoid token limits. A daily dev->main merge
        # lists every change of the day in its body, so it gets a far larger
        # budget: cutting it at 600 chars dropped all but the first two items.
        limit = 8000 if pr.get("title", "").startswith("Daily merge") else 600
        if len(body) > limit:
            body = body[:limit] + "..."
        author = pr.get("author", {}).get("login", "unknown")
        pr_summaries.append(
            f"- PR #{pr['number']}: {pr['title']} (by @{author})\n  Details: {body}"
        )

    pr_text = "\n\n".join(pr_summaries)

    system_prompt = (
        "You are an assistant generating release notes and changelog entries for TortoiseBots, "
        "an optional native PlayerBots C++ module for Tortoise WoW 1.18.1.\n"
        "Your audience consists of players, guild leaders, and private server operators.\n"
        "Rules:\n"
        "1. Group changes into clear, logical categories (e.g., 'Combat & AI', 'Starter Zones & World', 'Observability & Engine', 'Core Sync & Fixes'). Only include categories that have changes.\n"
        "2. Write concise, punchy, pragmatic bullet points explaining the gameplay or stability impact.\n"
        "3. Always reference the pull request as a markdown link like [#123](https://github.com/Sagiroth/TortoiseBots/pull/123) at the end of each bullet point.\n"
        "4. Do NOT output fluff, introductions, greetings, or sign-offs. Output ONLY the categorized markdown bullets starting with category headers (### Category).\n"
        "5. Keep the tone pragmatic and developer/gamer friendly.\n"
        "6. When a pull request body lists several changes (one line per pull request number, e.g. '- #456 ...'), write one bullet per listed change, "
        "link the listed pull request number (not the daily merge PR), and never drop or merge away any listed change."
    )

    user_prompt = f"Summarize the following merged pull requests:\n\n{pr_text}"

    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt}
        ],
        "temperature": 0.3
    }

    if base_url.endswith("/chat/completions"):
        url = base_url
    else:
        url = f"{base_url.rstrip('/')}/chat/completions"
    headers = {
        "Content-Type": "application/json",
        "Authorization": f"Bearer {api_key}",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36",
        "x-opencode-session": str(uuid.uuid4())
    }

    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers=headers,
        method="POST"
    )

    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            content = data["choices"][0]["message"]["content"].strip()
            return content
    except urllib.error.HTTPError as e:
        error_body = e.read().decode("utf-8")
        raise RuntimeError(f"OpenCode API error ({e.code}): {error_body}")
    except Exception as e:
        raise RuntimeError(f"Failed to communicate with OpenCode API: {e}")


def update_or_prepend_changelog(date_str, summary_text):
    """Update existing section for date_str by appending, or prepend a new section to CHANGELOG.md."""
    if os.path.exists(CHANGELOG_PATH):
        with open(CHANGELOG_PATH, "r", encoding="utf-8") as f:
            content = f.read()

        # Check if an entry for this date already exists (up to next ## section or EOF)
        pattern = rf"(##\s+{re.escape(date_str)}\s*\n)(.*?)(?=\n##\s+|\Z)"
        match = re.search(pattern, content, re.DOTALL)
        if match:
            existing_section = match.group(2).strip()
            if summary_text.strip() not in existing_section:
                merged_section = f"{existing_section}\n\n{summary_text}\n"
                new_content = content[:match.start(2)] + merged_section + content[match.end(2):]
                with open(CHANGELOG_PATH, "w", encoding="utf-8") as f:
                    f.write(new_content.strip() + "\n")
                print(f"Appended changes to existing section for {date_str} in {CHANGELOG_PATH}")
            else:
                print(f"Summary already present in section for {date_str} in {CHANGELOG_PATH}")
            return

        # Prepend new section before the first existing '## ' entry
        header = f"## {date_str}\n\n{summary_text}\n\n---\n\n"
        first_section = re.search(r"^##\s+", content, re.MULTILINE)
        if first_section:
            idx = first_section.start()
            new_content = content[:idx] + header + content[idx:]
        else:
            new_content = content.rstrip() + "\n\n" + header
    else:
        new_content = f"# Changelog\n\nAll notable changes to TortoiseBots are documented here.\n\n## {date_str}\n\n{summary_text}\n"

    with open(CHANGELOG_PATH, "w", encoding="utf-8") as f:
        f.write(new_content.strip() + "\n")
    print(f"Added new release section for {date_str} in {CHANGELOG_PATH}")


VERSION_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "VERSION")

BOT_COMMIT_AUTHOR = "github-actions[bot]"
SKIP_CI_MARKER = "[skip ci]"

# Matches the header written by with_build_range below.
BUILD_RANGE_RE = re.compile(r"^Builds \d{4}-\d{2}-\d{2}-v\d+ \S+ v\d+\s*$", re.MULTILINE)


def parse_commit_line(line):
    """Parse one `git log --format=%H|%cI|%an|%s` line (subject may contain '|')."""
    parts = line.split("|", 3)
    if len(parts) != 4 or not parts[0] or not parts[1]:
        return None
    return {"sha": parts[0], "date": parts[1], "author": parts[2], "subject": parts[3]}


def iter_first_parent_commits(ref="HEAD"):
    """Read first-parent history as parsed commit dicts (newest first)."""
    try:
        out = subprocess.check_output(
            ["git", "log", "--first-parent", "--format=%H|%cI|%an|%s", ref],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except Exception as e:
        print(f"Warning: could not read git history ({e}); defaulting build number to 1")
        return []
    commits = []
    for line in out.splitlines():
        parsed = parse_commit_line(line)
        if parsed:
            commits.append(parsed)
    return commits


def is_real_merge(commit):
    """True for real merges/commits; excludes the workflow's own bot commits."""
    return commit["author"] != BOT_COMMIT_AUTHOR and SKIP_CI_MARKER not in commit["subject"]


def commit_utc_day(iso_date):
    """Return the UTC calendar day (YYYY-MM-DD) for an ISO-8601 commit date."""
    try:
        dt = datetime.datetime.fromisoformat(iso_date)
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=datetime.timezone.utc)
        return dt.astimezone(datetime.timezone.utc).strftime("%Y-%m-%d")
    except Exception:
        return iso_date[:10]


def count_real_commits_on_day(commits, day):
    """Nth-merge counter: real mainline commits whose UTC day is `day`."""
    return sum(1 for c in commits if is_real_merge(c) and commit_utc_day(c["date"]) == day)


def compute_build_version(commits, day):
    """Return (version, number) with version `<UTC date>-v<N>`, N >= 1."""
    n = max(count_real_commits_on_day(commits, day), 1)
    return f"{day}-v{n}", n


def with_build_range(notes, date_str, build_number):
    """Prepend the day's build range header, replacing any stale one."""
    header = f"Builds {date_str}-v1 \u2013 v{build_number}"
    body = BUILD_RANGE_RE.sub("", notes or "").strip()
    if body:
        return f"{header}\n\n{body}\n"
    return f"{header}\n"



def release_exists_on_github(tag_name):
    """Check whether a release for tag_name already exists using gh CLI."""
    try:
        res = subprocess.run(
            ["gh", "release", "view", tag_name, "--json", "body"],
            capture_output=True,
            text=True
        )
        if res.returncode == 0:
            data = json.loads(res.stdout)
            return True, data.get("body", "")
    except Exception:
        pass
    return False, ""


def main():
    parser = argparse.ArgumentParser(description="Generate release notes from merged PRs using OpenCode AI.")
    parser.add_argument("--since", help="ISO timestamp or date to search PRs since (default: auto-detect from last tag/changelog)")
    parser.add_argument("--write", action="store_true", help="Prepend or append generated entry to CHANGELOG.md")
    parser.add_argument("--out-notes", help="Write release notes to specified file (useful for gh release create)")
    parser.add_argument("--out-delta", help="Write delta release notes for current run only (useful for Discord notifications)")
    parser.add_argument("--dry-run", action="store_true", help="Print collected PRs without calling AI API")
    parser.add_argument("--date", default=datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d"), help="Release date string (default: today)")
    parser.add_argument("--out-version", help="Write the per-merge build version (<UTC date>-v<N>) to the given file (e.g. VERSION)")
    parser.add_argument("--out-build-number", help="Write the day's build number N to the given file")
    parser.add_argument("--version-date", help="UTC date for the build version (default: --date)")
    parser.add_argument("--prs", help="Comma-separated PR numbers to summarize instead of auto-detecting (manual re-post; skips the CHANGELOG.md de-duplication)")
    args = parser.parse_args()

    version_day = args.version_date or args.date
    commits = iter_first_parent_commits()
    build_version, build_number = compute_build_version(commits, version_day)
    print(f"Build version for {version_day}: {build_version}")

    if args.out_version:
        with open(args.out_version, "w", encoding="utf-8") as f:
            f.write(build_version + "\n")
        print(f"Saved build version to: {args.out_version}")

    if args.out_build_number:
        with open(args.out_build_number, "w", encoding="utf-8") as f:
            f.write(str(build_number) + "\n")
        print(f"Saved build number to: {args.out_build_number}")

    api_key = os.environ.get("OPENCODE_API_KEY")
    base_url = os.environ.get("OPENCODE_BASE_URL", DEFAULT_BASE_URL)
    model = os.environ.get("OPENCODE_MODEL", DEFAULT_MODEL)

    since_tag, since_date = None, None
    if args.since:
        since_date = args.since
    else:
        since_tag, since_date = get_last_release_info()

    if args.prs:
        prs = []
        for number in [n.strip() for n in args.prs.split(",") if n.strip()]:
            output = subprocess.check_output(
                ["gh", "pr", "view", number, "--json", "number,title,body,mergedAt,author,url"], text=True)
            prs.append(json.loads(output))
        print(f"Summarizing requested PRs: {args.prs}")
    else:
        print(f"Checking for merged PRs since: {since_date or 'beginning'} (reference: {since_tag or 'none'})")
        prs = get_merged_prs_since(since_date)

    # Exclude any PRs that are already documented in CHANGELOG.md
    if not args.prs and os.path.exists(CHANGELOG_PATH):
        with open(CHANGELOG_PATH, "r", encoding="utf-8") as f:
            changelog_content = f.read()
        filtered_prs = []
        for pr in prs:
            pr_pattern = rf"#\s*{pr['number']}\b"
            if not re.search(pr_pattern, changelog_content):
                filtered_prs.append(pr)
            else:
                print(f"Skipping PR #{pr['number']} (already documented in CHANGELOG.md)")
        prs = filtered_prs

    if not prs:
        print("No new merged PRs found since last release/entry. Nothing to do.")
        # Set GitHub Action output if in GHA environment
        if "GITHUB_OUTPUT" in os.environ:
            with open(os.environ["GITHUB_OUTPUT"], "a") as gh_out:
                gh_out.write("has_changes=false\n")
        sys.exit(0)

    print(f"Found {len(prs)} merged PR(s) to summarize:")
    for pr in prs:
        print(f"  #{pr['number']}: {pr['title']}")

    if args.dry_run:
        print("\n[Dry Run] PRs collected successfully. Skipping AI API call.")
        sys.exit(0)

    if not api_key or not base_url or not model:
        print("Error: OPENCODE_API_KEY, OPENCODE_ENDPOINT, and OPENCODE_MODEL environment variables are required.", file=sys.stderr)
        sys.exit(1)

    print("\nRequesting AI summary from configured provider...")
    summary = generate_summary_with_ai(prs, api_key, base_url, model)
    print("\nGenerated Changelog:\n")
    print(summary)
    print("\n" + "=" * 40)

    release_tag = f"v{args.date}"
    exists, existing_body = release_exists_on_github(release_tag)

    # The daily release notes always open with the day's build range so players
    # can map the release to the per-merge build tags (e.g. Builds 2026-09-25-v1 – v7).
    base_body = existing_body.strip() if exists and existing_body else ""
    ranged_base = with_build_range(base_body, args.date, build_number)
    notes_to_write = ranged_base.rstrip() + "\n\n" + summary.strip() + "\n"
    if args.out_notes:
        with open(args.out_notes, "w", encoding="utf-8") as f:
            f.write(notes_to_write.strip() + "\n")
        print(f"Saved release notes to: {args.out_notes} (merged_with_existing={exists})")

    if args.out_delta:
        with open(args.out_delta, "w", encoding="utf-8") as f:
            f.write(summary.strip() + "\n")
        print(f"Saved delta release notes to: {args.out_delta}")

    if args.write:
        update_or_prepend_changelog(args.date, summary)

    if "GITHUB_OUTPUT" in os.environ:
        with open(os.environ["GITHUB_OUTPUT"], "a") as gh_out:
            gh_out.write("has_changes=true\n")
            gh_out.write(f"release_tag={release_tag}\n")
            gh_out.write(f"release_title=TortoiseBots ({args.date})\n")
            gh_out.write(f"release_exists={'true' if exists else 'false'}\n")


if __name__ == "__main__":
    main()
