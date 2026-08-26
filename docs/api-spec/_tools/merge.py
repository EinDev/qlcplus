#!/usr/bin/env python3
"""
Merges docs/api-spec/fragments/_skeleton.yaml plus all seven domain fragments
into one AsyncAPI 3.0 document at docs/api-spec/qlcplus-api.yaml.

For each fragment:
  - fragment['messages']  -> components.messages   (collision = hard error)
  - fragment['schemas']   -> components.schemas    (collision = hard error)
  - fragment['operations']-> operations             (collision = hard error)
  - for every message key added, also add a lowerCamelCase entry under
    channels.qlcplus.messages -> components.messages/<Key> (per
    00-conventions.md: this is what lets operations' $refs resolve).

Then validates every $ref in the merged document actually resolves to a
real key in the document, and reports method/topic name collisions across
domains as a sanity check (there should be none, given the per-domain
prefix discipline - if there are, that's a real bug worth investigating,
not something to silently paper over).
"""
import sys
from pathlib import Path

import yaml

SPEC_DIR = Path(__file__).resolve().parent.parent
FRAG_DIR = SPEC_DIR / "fragments"
OUT_PATH = SPEC_DIR / "qlcplus-api.yaml"

FRAGMENTS = [
    "core.yaml",
    "fixtures.yaml",
    "fixturedefs.yaml",
    "functions-core.yaml",
    "functions-advanced.yaml",
    "io.yaml",
    "virtualconsole.yaml",
]


def lower_first(s):
    return s[0].lower() + s[1:]


def load(name):
    return yaml.safe_load((FRAG_DIR / name).read_text(encoding="utf-8"))


def main():
    skeleton = load("_skeleton.yaml")

    components_messages = skeleton["components"]["messages"]
    components_schemas = skeleton["components"]["schemas"] or {}
    top_operations = skeleton["operations"]
    channel_messages = skeleton["channels"]["qlcplus"]["messages"]

    all_methods = {}   # method/topic const -> (domain, message key), for collision detection
    all_topics = {}

    for frag_name in FRAGMENTS:
        frag = load(frag_name)
        f_messages = frag.get("messages", {}) or {}
        f_schemas = frag.get("schemas", {}) or {}
        f_operations = frag.get("operations", {}) or {}

        for key, mdef in f_messages.items():
            if key in components_messages:
                sys.exit(f"FATAL: message key collision on '{key}' (from {frag_name}) - already defined")
            components_messages[key] = mdef

            # channel message key = lowerCamelCase(component key)
            ck = lower_first(key)
            if ck in channel_messages:
                sys.exit(f"FATAL: channel message key collision on '{ck}' (from {frag_name})")
            channel_messages[ck] = {"$ref": f"#/components/messages/{key}"}

            # collision-check method/topic consts (informational domain tag = filename)
            try:
                props = mdef["payload"]["properties"]
                const = props.get("method", {}).get("const") or props.get("topic", {}).get("const")
                is_method = "method" in props
            except (KeyError, TypeError):
                const, is_method = None, None
            if const:
                bucket = all_methods if is_method else all_topics
                if const in bucket and bucket[const][0] != frag_name:
                    print(f"! WARNING: duplicate {'method' if is_method else 'topic'} const '{const}' "
                          f"in both {bucket[const][0]} ({bucket[const][1]}) and {frag_name} ({key})", file=sys.stderr)
                bucket[const] = (frag_name, key)

        for key, sdef in f_schemas.items():
            if key in components_schemas:
                sys.exit(f"FATAL: schema key collision on '{key}' (from {frag_name}) - already defined")
            components_schemas[key] = sdef

        for key, odef in f_operations.items():
            if key in top_operations:
                sys.exit(f"FATAL: operation key collision on '{key}' (from {frag_name}) - already defined")
            top_operations[key] = odef

        print(f"{frag_name}: +{len(f_messages)} messages, +{len(f_schemas)} schemas, +{len(f_operations)} operations")

    skeleton["components"]["messages"] = components_messages
    skeleton["components"]["schemas"] = components_schemas
    skeleton["operations"] = top_operations
    skeleton["channels"]["qlcplus"]["messages"] = channel_messages

    # Cosmetic: source fragments mix `description: >` (folded) and plain
    # multi-line strings; PyYAML round-trips those as single-quoted scalars
    # with an ugly trailing blank-line-then-quote. Collapse embedded
    # newlines/whitespace in every string value to single spaces so the
    # dumper picks a clean style - these are all prose, never data that
    # depends on literal newlines.
    def collapse_whitespace(node):
        if isinstance(node, dict):
            return {k: collapse_whitespace(v) for k, v in node.items()}
        if isinstance(node, list):
            return [collapse_whitespace(v) for v in node]
        if isinstance(node, str) and "\n" in node:
            return " ".join(node.split())
        return node

    skeleton = collapse_whitespace(skeleton)

    print(f"\nTOTAL: {len(components_messages)} messages, {len(components_schemas)} schemas, "
          f"{len(top_operations)} operations, {len(channel_messages)} channel message keys")

    # --- validate every $ref resolves within the merged document ---
    def resolve(ref, root):
        assert ref.startswith("#/"), f"unsupported ref form: {ref}"
        node = root
        for part in ref[2:].split("/"):
            part = part.replace("~1", "/").replace("~0", "~")
            if not isinstance(node, dict) or part not in node:
                return False
            node = node[part]
        return True

    broken = []

    def walk(node, path):
        if isinstance(node, dict):
            if "$ref" in node and isinstance(node["$ref"], str):
                if not resolve(node["$ref"], skeleton):
                    broken.append((path, node["$ref"]))
            for k, v in node.items():
                walk(v, f"{path}/{k}")
        elif isinstance(node, list):
            for i, v in enumerate(node):
                walk(v, f"{path}[{i}]")

    walk(skeleton, "")

    if broken:
        print(f"\n{len(broken)} BROKEN $ref(s):", file=sys.stderr)
        for path, ref in broken:
            print(f"  {path} -> {ref}", file=sys.stderr)
        sys.exit(1)
    else:
        print("\nAll $refs resolve. OK.")

    OUT_PATH.write_text(
        yaml.safe_dump(skeleton, sort_keys=False, allow_unicode=True, width=100),
        encoding="utf-8",
    )
    print(f"\nWrote {OUT_PATH}")


if __name__ == "__main__":
    main()
