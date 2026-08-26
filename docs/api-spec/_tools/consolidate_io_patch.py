#!/usr/bin/env python3
"""
io.patch.{input,output,feedback}.set and .remove each independently attach/
detach a plugin line (or, for .remove, just release the patch) on a
universe. Verified field-by-field before merging (per MERGE-PLAN.md
discipline) rather than assuming the similar names meant similar shapes:

  - .remove: io.patch.input.remove and io.patch.feedback.remove are
    BYTE-FOR-BYTE IDENTICAL - both are singleton patches per universe, so
    params are just {universeId, baseRevision}. io.patch.output.remove
    additionally requires `index` because output patches are an *array*
    (Universe::outputPatch(index)) - a universe can have >1 output patch,
    but only ever one input patch and one feedback patch. All three already
    shared the same IoPatchMutationOkResponse for their ok-branch.
  - .set: io.patch.input.set and io.patch.output.set/io.patch.feedback.set
    all attach a plugin line to a universe. output.set additionally has an
    optional `index` (same array-slot reasoning as .remove); input.set
    additionally has an optional `profileName` (input-profile attachment -
    meaningless for an output/feedback line). The line-index field itself
    is even named differently per type ("input" vs "output") despite
    meaning the same thing (a QLCIOPlugin line index, see
    io.plugin.getLines).

Both collapse into one generic method discriminated by a `patchType` enum
([input, output, feedback]) - the exact same enum already used by the
existing io.patch.setParameters method in this fragment. This isn't an
invented abstraction: it applies io.patch.setParameters' own already-
accepted pattern to the two sibling operations (attach/detach) that predate
it. `index`/`profileName` stay optional, documented per-patchType via
description text rather than schema-enforced - the same risk level
setParameters already accepted for `index` ("ignored for input/feedback"),
not a new laxity introduced by this pass.

Deliberately NOT touched: io.patch.output.setState (live/§4b paused+
blackout toggle - a different tier of state from the §4a set/remove
structural mutations here, and has no equivalent at all for input/feedback
patches - genuinely singleton to output patches, not a redundant near-miss).
io.patch.input.set/output.set/feedback.set's shared IoPatchMutationOkResponse
was already deduplicated before this pass (nothing to do there).

Modifies io.yaml in place.
"""
from pathlib import Path
import yaml

FRAG = Path(__file__).resolve().parent.parent / "fragments" / "io.yaml"

TYPES = ["Input", "Output", "Feedback"]
PATCH_TYPE_ENUM = ["input", "output", "feedback"]


def load():
    return yaml.safe_load(FRAG.read_text(encoding="utf-8"))


def lower_first(s):
    return s[0].lower() + s[1:]


def remove_message_and_ops(messages, operations, key):
    messages.pop(key, None)
    ck = lower_first(key)
    dead_ops = [ok for ok, odef in operations.items()
                if any(m.get("$ref", "").endswith("/messages/" + ck) for m in odef.get("messages", []))]
    for ok in dead_ops:
        operations.pop(ok, None)
    return dead_ops


def add_op_pair(doc, req_key, resp_key):
    operations = doc["operations"]
    ck_req, ck_resp = lower_first(req_key), lower_first(resp_key)
    operations[f"send{req_key}"] = {
        "action": "send",
        "channel": {"$ref": "#/channels/qlcplus"},
        "messages": [{"$ref": f"#/channels/qlcplus/messages/{ck_req}"}],
    }
    operations[f"receive{resp_key}"] = {
        "action": "receive",
        "channel": {"$ref": "#/channels/qlcplus"},
        "messages": [{"$ref": f"#/channels/qlcplus/messages/{ck_resp}"}],
    }


def merge_patch_remove(doc):
    messages, operations = doc["messages"], doc["operations"]

    # Response payload is already identical across all three types (all
    # reuse IoPatchMutationOkResponse) - keep the first type's as the
    # template, matching consolidate_vc_presets.py's merge_identical().
    response_template = messages["IoPatchInputRemoveResponse"]

    removed_msgs, removed_ops = [], []
    for t in TYPES:
        for suffix in ("RemoveRequest", "RemoveResponse"):
            key = f"IoPatch{t}{suffix}"
            if key in messages:
                removed_msgs.append(key)
                removed_ops += remove_message_and_ops(messages, operations, key)

    messages["IoPatchRemoveRequest"] = {"payload": {
        "type": "object",
        "required": ["type", "id", "method", "params"],
        "properties": {
            "type": {"const": "request"},
            "id": {"type": "string"},
            "method": {"const": "io.patch.remove"},
            "params": {
                "type": "object",
                "required": ["universeId", "patchType", "baseRevision"],
                "description": (
                    "Detaches a patch from a universe. patchType selects which "
                    "one: input and feedback are singleton per universe (index "
                    "not used); output patches are an indexed array, so `index` "
                    "is required when patchType is 'output'."
                ),
                "properties": {
                    "universeId": {"type": "integer"},
                    "patchType": {"enum": list(PATCH_TYPE_ENUM)},
                    "index": {
                        "type": "integer",
                        "description": "Output patch slot index; required when patchType='output', unused otherwise.",
                    },
                    "baseRevision": {"type": "integer"},
                },
            },
        },
    }}
    messages["IoPatchRemoveResponse"] = response_template

    add_op_pair(doc, "IoPatchRemoveRequest", "IoPatchRemoveResponse")

    print(f"  patch.remove: removed {removed_msgs} + ops {removed_ops}; added IoPatchRemoveRequest/Response")


def merge_patch_set(doc):
    messages, operations = doc["messages"], doc["operations"]

    response_template = messages["IoPatchInputSetResponse"]

    removed_msgs, removed_ops = [], []
    for t in TYPES:
        for suffix in ("SetRequest", "SetResponse"):
            key = f"IoPatch{t}{suffix}"
            if key in messages:
                removed_msgs.append(key)
                removed_ops += remove_message_and_ops(messages, operations, key)

    messages["IoPatchSetRequest"] = {"payload": {
        "type": "object",
        "required": ["type", "id", "method", "params"],
        "properties": {
            "type": {"const": "request"},
            "id": {"type": "string"},
            "method": {"const": "io.patch.set"},
            "params": {
                "type": "object",
                "required": ["universeId", "patchType", "pluginName", "line", "baseRevision"],
                "description": (
                    "Attaches a plugin line to a universe's patch. patchType "
                    "selects which one (input/output/feedback - the plugin must "
                    "advertise the Feedback capability for patchType='feedback', "
                    "see io.plugin.list). `line` is the plugin's line index "
                    "either way (see io.plugin.getLines) - the input line for "
                    "patchType='input', the output line for 'output'/'feedback'."
                ),
                "properties": {
                    "universeId": {"type": "integer"},
                    "patchType": {"enum": list(PATCH_TYPE_ENUM)},
                    "pluginName": {"type": "string"},
                    "line": {
                        "type": "integer",
                        "description": "Plugin input/output line index (see io.plugin.getLines).",
                    },
                    "index": {
                        "type": "integer",
                        "description": (
                            "Output patch slot index; only used when patchType='output'. "
                            "Omit (or pass outputPatchesCount) to append a new patch; pass "
                            "an existing index to replace it."
                        ),
                    },
                    "profileName": {
                        "type": "string",
                        "description": (
                            "Input profile name to attach (see io.inputProfile.*); only "
                            "used when patchType='input'. Omit/empty for none."
                        ),
                    },
                    "baseRevision": {"type": "integer"},
                },
            },
        },
    }}
    messages["IoPatchSetResponse"] = response_template

    add_op_pair(doc, "IoPatchSetRequest", "IoPatchSetResponse")

    print(f"  patch.set: removed {removed_msgs} + ops {removed_ops}; added IoPatchSetRequest/Response")


def main():
    doc = load()
    before_msgs, before_ops = len(doc["messages"]), len(doc["operations"])

    print("Merging patch.remove...")
    merge_patch_remove(doc)

    print("Merging patch.set...")
    merge_patch_set(doc)

    doc_out = {"messages": doc["messages"], "schemas": doc["schemas"], "operations": doc["operations"]}
    FRAG.write_text(yaml.safe_dump(doc_out, sort_keys=False, allow_unicode=True, width=100), encoding="utf-8")

    print(f"\nWrote {FRAG}")
    print(f"messages {before_msgs} -> {len(doc['messages'])}   operations {before_ops} -> {len(doc['operations'])}")


if __name__ == "__main__":
    main()
