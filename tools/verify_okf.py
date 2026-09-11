#!/usr/bin/env python3
"""
verify_okf.py — Open Knowledge Format (OKF) validator for TortoiseBots docs.

Validates that:
1. docs/manifest.yaml exists, parses as valid YAML, and contains valid metadata and entrypoints.
2. Every document in the knowledge base has valid YAML frontmatter conforming to schema.
3. Every referenced id in 'relates_to' and 'depends_on' resolves to an existing node id.
4. Internal markdown links [text](path.md) resolve to existing files.
"""

import os
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: PyYAML is required. Please install pyyaml.")
    sys.exit(1)

DOCS_DIR = Path(__file__).resolve().parent.parent / "docs"
MANIFEST_FILE = DOCS_DIR / "manifest.yaml"

REQUIRED_FIELDS = ["id", "title", "category", "summary", "tags"]
VALID_CATEGORIES = {"classes", "guides", "concepts", "reference"}

FRONTMATTER_REGEX = re.compile(r"^---\s*\n(.*?)\n---\s*\n", re.DOTALL)
MD_LINK_REGEX = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")

def extract_frontmatter(file_path):
    text = file_path.read_text(encoding="utf-8")
    match = FRONTMATTER_REGEX.match(text)
    if not match:
        return None, text
    raw_yaml = match.group(1)
    body = text[match.end():]
    try:
        data = yaml.safe_load(raw_yaml)
        if not isinstance(data, dict):
            return None, text
        return data, body
    except yaml.YAMLError as e:
        print(f"YAML parse error in {file_path}: {e}")
        return None, text

def check_manifest(manifest_path, node_ids):
    errors = []
    if not manifest_path.is_file():
        errors.append(f"Missing manifest file at {manifest_path}")
        return errors

    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = yaml.safe_load(f)
    except yaml.YAMLError as e:
        errors.append(f"Error parsing manifest.yaml: {e}")
        return errors

    if not isinstance(manifest, dict):
        errors.append("manifest.yaml must be a YAML mapping/dictionary")
        return errors

    for req in ["bundle", "version", "entrypoints", "categories"]:
        if req not in manifest:
            errors.append(f"manifest.yaml missing required key: {req}")

    if "entrypoints" in manifest and isinstance(manifest["entrypoints"], dict):
        for ep_name, ep_relpath in manifest["entrypoints"].items():
            target = DOCS_DIR / ep_relpath
            if not target.exists():
                errors.append(f"Entrypoint '{ep_name}' points to non-existent file: {target}")

    return errors

def main():
    errors = []
    nodes_by_id = {}
    files_by_id = {}
    bodies_by_file = {}

    md_files = list(DOCS_DIR.glob("**/*.md"))
    # Exclude archive or external directories if any
    filtered_files = [f for f in md_files if "archive" not in f.parts]

    for md_file in filtered_files:
        rel_path = md_file.relative_to(DOCS_DIR)
        # Skip top-level README.md from strict frontmatter check if it acts as human landing page,
        # but check it if it has frontmatter.
        frontmatter, body = extract_frontmatter(md_file)
        if frontmatter is None:
            if md_file.name == "README.md":
                bodies_by_file[md_file] = body
                continue
            errors.append(f"Missing or invalid YAML frontmatter in {rel_path}")
            continue

        for req in REQUIRED_FIELDS:
            if req not in frontmatter:
                errors.append(f"{rel_path}: Missing required frontmatter field '{req}'")

        node_id = frontmatter.get("id")
        if node_id:
            if node_id in nodes_by_id:
                errors.append(f"Duplicate node id '{node_id}' in {rel_path} and {files_by_id[node_id]}")
            else:
                nodes_by_id[node_id] = frontmatter
                files_by_id[node_id] = md_file

        cat = frontmatter.get("category")
        if cat and cat not in VALID_CATEGORIES:
            errors.append(f"{rel_path}: Invalid category '{cat}'. Expected one of {VALID_CATEGORIES}")

        bodies_by_file[md_file] = body

    # Verify cross-reference graph edges (relates_to, depends_on)
    for node_id, fm in nodes_by_id.items():
        rel_file = files_by_id[node_id].relative_to(DOCS_DIR)
        for edge_type in ["relates_to", "depends_on"]:
            targets = fm.get(edge_type, [])
            if not isinstance(targets, list):
                errors.append(f"{rel_file}: '{edge_type}' must be a list of node IDs")
                continue
            for target_id in targets:
                if target_id not in nodes_by_id:
                    errors.append(f"{rel_file}: '{edge_type}' references unknown node ID '{target_id}'")

    # Verify markdown links
    for md_file, body in bodies_by_file.items():
        rel_file = md_file.relative_to(DOCS_DIR)
        for match in MD_LINK_REGEX.finditer(body):
            text, href = match.group(1), match.group(2)
            # Skip web URLs, file URIs, and mailto/anchor-only links
            if href.startswith(("http://", "https://", "mailto:", "#", "file://")):
                continue
            # Strip fragment
            link_path = href.split("#")[0]
            if not link_path:
                continue
            # Resolve relative to file directory
            target_path = (md_file.parent / link_path).resolve()
            if not target_path.exists():
                errors.append(f"{rel_file}: Broken internal markdown link [{text}]({href})")

    # Verify manifest
    manifest_errors = check_manifest(MANIFEST_FILE, set(nodes_by_id.keys()))
    errors.extend(manifest_errors)

    if errors:
        print(f"OKF verification FAILED with {len(errors)} errors:")
        for err in errors:
            print(f"  - {err}")
        sys.exit(1)
    else:
        print(f"OKF verification PASSED ({len(nodes_by_id)} nodes validated, graph edges consistent).")
        sys.exit(0)

if __name__ == "__main__":
    main()
