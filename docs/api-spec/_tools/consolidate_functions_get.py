#!/usr/bin/env python3
"""
Folds the 10 per-type `functions.<type>.get` methods (Scene, Chaser, EFX,
Collection, Sequence, Script, RGBMatrix, Show, Audio, Video - the actual
Function::Type subtypes) into the generic `functions.get` by adding a
discriminated `typeDetail` field to FunctionsDetail. Removes the now-redundant
per-type Get Request/Response/OkResponse messages and their operations.

Deliberately does NOT touch functions.channelsgroup.get - ChannelsGroup is
not a Function subtype (confirmed: it has no `type` in FunctionsTypeEnum and
functions.get takes a `functionId`, which a channels-group has no analogue
of), so its .get is the *only* way to fetch one, not a redundant round-trip.

Modifies functions-core.yaml and functions-advanced.yaml in place.
"""
import sys
from pathlib import Path

import yaml

FRAG_DIR = Path(__file__).resolve().parent.parent / "fragments"
TARGETS = ["functions-core.yaml", "functions-advanced.yaml"]

# The 10 real Function::Type subtypes' method-name slugs. Exhaustive,
# explicit allow-list on purpose (see module docstring re: channelsgroup).
REAL_FUNCTION_TYPES = {
    "scene", "chaser", "efx", "collection", "sequence",
    "script", "rgbmatrix", "show", "audio", "video",
}


def get_method_const(msg_def):
    try:
        return msg_def["payload"]["properties"]["method"]["const"]
    except (KeyError, TypeError):
        return None


def lower_first(s):
    return s[0].lower() + s[1:]


def process(path: Path, type_detail_refs: list):
    doc = yaml.safe_load(path.read_text(encoding="utf-8"))
    messages = doc.get("messages", {})
    schemas = doc.get("schemas", {})
    operations = doc.get("operations", {})

    before_msgs, before_ops, before_schemas = len(messages), len(operations), len(schemas)

    # 1. Find the target requests: method == functions.<realtype>.get
    request_keys_to_remove = []
    for key, mdef in messages.items():
        const = get_method_const(mdef)
        if not const:
            continue
        parts = const.split(".")
        if len(parts) == 3 and parts[0] == "functions" and parts[2] == "get" and parts[1] in REAL_FUNCTION_TYPES:
            request_keys_to_remove.append((key, parts[1]))

    if not request_keys_to_remove:
        print(f"{path.name}: no matching functions.<type>.get requests found")
        return

    all_keys_to_remove = set()

    for req_key, type_slug in request_keys_to_remove:
        if not req_key.endswith("Request"):
            print(f"  ! SKIP {req_key}: doesn't end in 'Request'", file=sys.stderr)
            continue
        prefix = req_key[: -len("Request")]
        resp_key = prefix + "Response"
        ok_key = prefix + "OkResponse"

        all_keys_to_remove.add(req_key)
        if resp_key in messages:
            all_keys_to_remove.add(resp_key)

        if ok_key not in messages:
            print(f"  ! SKIP {type_slug}: no {ok_key} found", file=sys.stderr)
            continue
        all_keys_to_remove.add(ok_key)

        result_schema = messages[ok_key]["payload"]["properties"]["result"]

        if "$ref" in result_schema:
            # Already a clean named schema (functions-core.yaml's pattern) -
            # reuse it as-is, no structural change needed.
            type_detail_refs.append(result_schema["$ref"])
            print(f"  {type_slug}: reusing existing schema {result_schema['$ref']}")
        else:
            # Inline object (functions-advanced.yaml's pattern, e.g. Script's
            # {functionId, source, docRevision}) - synthesize a standalone
            # named schema by relocating this exact dict, verbatim, into
            # `schemas`. No field-level editing (leaves functionId/
            # docRevision duplicated inside - see notes.md; correctness over
            # cosmetic minimalism for this pass).
            schema_name = f"Functions{type_slug.capitalize()}Detail"
            if type_slug == "rgbmatrix":
                schema_name = "FunctionsRgbMatrixDetail"
            if schema_name in schemas:
                print(f"  ! WARNING {type_slug}: synthesized name {schema_name} already exists in schemas, skipping synthesis (using existing)", file=sys.stderr)
            else:
                schemas[schema_name] = result_schema
                print(f"  {type_slug}: synthesized new schema {schema_name} from inline result")
            type_detail_refs.append(f"#/components/schemas/{schema_name}")

    # 2. Remove those messages.
    for k in all_keys_to_remove:
        messages.pop(k, None)

    # 3. Remove operations that reference any removed message (by channel
    #    message key, i.e. lowerCamelCase of the component key).
    removed_channel_keys = {lower_first(k) for k in all_keys_to_remove}

    def op_refs_removed_message(op):
        for m in op.get("messages", []):
            ref = m.get("$ref", "")
            if any(ref.endswith("/messages/" + ck) for ck in removed_channel_keys):
                return True
        return False

    op_keys_to_remove = [ok for ok, odef in operations.items() if op_refs_removed_message(odef)]
    for ok in op_keys_to_remove:
        operations.pop(ok, None)

    print(f"  removed messages: {sorted(all_keys_to_remove)}")
    print(f"  removed operations: {sorted(op_keys_to_remove)}")

    doc["messages"], doc["schemas"], doc["operations"] = messages, schemas, operations
    path.write_text(yaml.safe_dump(doc, sort_keys=False, allow_unicode=True, width=100), encoding="utf-8")
    print(f"{path.name}: messages {before_msgs}->{len(messages)}  operations {before_ops}->{len(operations)}  schemas {before_schemas}->{len(schemas)}")


def add_type_detail_to_functions_detail(refs: list):
    core_path = FRAG_DIR / "functions-core.yaml"
    doc = yaml.safe_load(core_path.read_text(encoding="utf-8"))
    detail = doc["schemas"]["FunctionsDetail"]

    detail["properties"]["typeDetail"] = {
        "description": (
            "Type-specific detail, discriminated by `type` above. Replaces the "
            "old per-type functions.<type>.get round-trip - this single "
            "functions.get call now returns everything. Each branch still "
            "carries its own functionId/docRevision fields as originally "
            "authored (redundant with .id / the show's docRevision - left "
            "as-is by the consolidation pass rather than risk editing field "
            "semantics; a cosmetic follow-up, not a correctness issue)."
        ),
        "oneOf": [{"$ref": r} for r in sorted(set(refs))],
    }
    core_path.write_text(yaml.safe_dump(doc, sort_keys=False, allow_unicode=True, width=100), encoding="utf-8")
    print(f"functions-core.yaml: FunctionsDetail.typeDetail.oneOf <- {len(set(refs))} refs")


if __name__ == "__main__":
    collected_refs: list = []
    for name in TARGETS:
        process(FRAG_DIR / name, collected_refs)
    if len(collected_refs) != 10:
        print(f"! WARNING: expected 10 typeDetail refs, got {len(collected_refs)}: {collected_refs}", file=sys.stderr)
    add_type_detail_to_functions_detail(collected_refs)
