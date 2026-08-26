#!/usr/bin/env python3
"""
Three widget types (VcXyPad, VcSpeedDial, VcAnimation) each reinvented their
own "named preset list" CRUD as separate methods. Verified by hand (not
assumed) that:
  - preset.remove: identical params {widgetId, presetId, baseRevision} across
    all three, and all three already reuse the SHARED VcDocRevisionOkResponse.
  - applyPreset (a §4b live action, no baseRevision): identical params
    {widgetId, presetId} across all three, all reuse GenericAckResponse.
  - preset.add: genuinely different payload per type (xyPad: presetType enum +
    function/fixtureGroup refs; speedDial: name+valueMs; animation: presetType
    enum + color/text/algorithm fields) - BUT the OkResponse shape
    ({docRevision, presetId}) is identical, and in fact speedDial's and
    animation's Response messages already (buggily) pointed at
    VcXyPadPresetAddOkResponse instead of having their own - a real spec bug,
    fixed as a side effect of this consolidation rather than perpetuated.

Collapses 3 methods -> 1 for remove and apply (no discriminated union needed,
shapes are identical), and 3 methods -> 1 for add (using the same
discriminated-union pattern as the functions.get consolidation: a `preset`
field that's a oneOf of three new per-type data schemas).

Explicitly NOT merged (checked, genuinely asymmetric across the three types,
not just superficially different): preset.move/preset.rename (xyPad only),
preset.update (speedDial only) - these don't have equivalents in the other
two types, so there is no real duplication to remove.

Modifies virtualconsole.yaml in place.
"""
from pathlib import Path
import yaml

FRAG = Path(__file__).resolve().parent.parent / "fragments" / "virtualconsole.yaml"

TYPES = ["XyPad", "SpeedDial", "Animation"]


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


def merge_identical(doc, op_name, request_suffix, response_suffix, new_method, new_prefix, needs_base_revision):
    """For an op (remove/apply) whose Request/Response shape is identical
    across all three types: keep the FIRST type's Request payload as the
    template for the new generic message, delete all three types' versions,
    add one new generic Request/Response pair."""
    messages, operations = doc["messages"], doc["operations"]

    template_key = f"Vc{TYPES[0]}{request_suffix}"
    template = messages[template_key]
    response_template_key = f"Vc{TYPES[0]}{response_suffix}"
    response_template = messages[response_template_key]

    removed_msgs, removed_ops = [], []
    for t in TYPES:
        for suffix in (request_suffix, response_suffix):
            key = f"Vc{t}{suffix}"
            if key in messages:
                removed_msgs.append(key)
                removed_ops += remove_message_and_ops(messages, operations, key)

    new_req_key = f"{new_prefix}Request"
    new_resp_key = f"{new_prefix}Response"

    new_req = {"payload": {
        "type": "object",
        "required": ["type", "id", "method", "params"],
        "properties": {
            "type": {"const": "request"},
            "id": {"type": "string"},
            "method": {"const": new_method},
            "params": template["payload"]["properties"]["params"],
        },
    }}
    messages[new_req_key] = new_req
    messages[new_resp_key] = response_template

    ck_req, ck_resp = lower_first(new_req_key), lower_first(new_resp_key)
    doc["channels_new_keys"].append((ck_req, new_req_key))
    doc["channels_new_keys"].append((ck_resp, new_resp_key))

    operations[f"send{new_req_key}"] = {
        "action": "send",
        "channel": {"$ref": "#/channels/qlcplus"},
        "messages": [{"$ref": f"#/channels/qlcplus/messages/{ck_req}"}],
    }
    operations[f"receive{new_resp_key}"] = {
        "action": "receive",
        "channel": {"$ref": "#/channels/qlcplus"},
        "messages": [{"$ref": f"#/channels/qlcplus/messages/{ck_resp}"}],
    }

    print(f"  {op_name}: removed {removed_msgs} + ops {removed_ops}; added {new_req_key}, {new_resp_key}")


def merge_preset_add(doc):
    messages, schemas, operations = doc["messages"], doc["schemas"], doc["operations"]

    data_schema_names = {}
    for t in TYPES:
        req_key = f"Vc{t}PresetAddRequest"
        params = messages[req_key]["payload"]["properties"]["params"]
        props = dict(params["properties"])
        props.pop("widgetId", None)
        props.pop("baseRevision", None)
        required = [r for r in params.get("required", []) if r not in ("widgetId", "baseRevision")]

        schema_name = f"Vc{t}PresetData"
        schemas[schema_name] = {
            "type": "object",
            "required": required,
            "properties": props,
        }
        if "description" in params:
            schemas[schema_name]["description"] = params["description"]
        data_schema_names[t] = schema_name

    removed_msgs, removed_ops = [], []
    for t in TYPES:
        for suffix in ("PresetAddRequest", "PresetAddResponse", "PresetAddOkResponse"):
            key = f"Vc{t}{suffix}"
            if key in messages:
                removed_msgs.append(key)
                removed_ops += remove_message_and_ops(messages, operations, key)

    messages["VcWidgetPresetAddRequest"] = {"payload": {
        "type": "object",
        "required": ["type", "id", "method", "params"],
        "properties": {
            "type": {"const": "request"},
            "id": {"type": "string"},
            "method": {"const": "vc.widget.preset.add"},
            "params": {
                "type": "object",
                "required": ["widgetId", "baseRevision", "preset"],
                "description": ("Adds a preset to widgetId's preset list. `preset`'s shape is "
                                 "determined by the widget's own type (looked up server-side from "
                                 "widgetId, not a separate discriminator field here) - see "
                                 f"{', '.join(data_schema_names.values())}."),
                "properties": {
                    "widgetId": {"type": "string"},
                    "baseRevision": {"type": "integer"},
                    "preset": {"oneOf": [{"$ref": f"#/components/schemas/{n}"} for n in data_schema_names.values()]},
                },
            },
        },
    }}
    messages["VcWidgetPresetAddResponse"] = {"payload": {
        "oneOf": [
            {"$ref": "#/components/messages/VcWidgetPresetAddOkResponse/payload"},
            {"$ref": "#/components/messages/ErrorResponse/payload"},
        ]
    }}
    messages["VcWidgetPresetAddOkResponse"] = {"payload": {
        "type": "object",
        "required": ["type", "id", "ok", "result"],
        "properties": {
            "type": {"const": "response"},
            "id": {"type": "string"},
            "ok": {"const": True},
            "result": {
                "type": "object",
                "required": ["docRevision", "presetId"],
                "properties": {
                    "docRevision": {"type": "integer"},
                    "presetId": {"type": "integer"},
                },
            },
        },
    }}

    for key in ("VcWidgetPresetAddRequest", "VcWidgetPresetAddResponse", "VcWidgetPresetAddOkResponse"):
        doc["channels_new_keys"].append((lower_first(key), key))

    operations["sendVcWidgetPresetAddRequest"] = {
        "action": "send",
        "channel": {"$ref": "#/channels/qlcplus"},
        "messages": [{"$ref": "#/channels/qlcplus/messages/vcWidgetPresetAddRequest"}],
    }
    operations["receiveVcWidgetPresetAddResponse"] = {
        "action": "receive",
        "channel": {"$ref": "#/channels/qlcplus"},
        "messages": [{"$ref": "#/channels/qlcplus/messages/vcWidgetPresetAddResponse"}],
    }

    print(f"  preset.add: removed {removed_msgs} + ops {removed_ops}; "
          f"added VcWidgetPresetAddRequest/Response/OkResponse + schemas {list(data_schema_names.values())}")


def main():
    doc = load()
    doc["channels_new_keys"] = []  # scratch, not part of the real fragment shape

    print("Merging preset.remove...")
    merge_identical(doc, "preset.remove", "PresetRemoveRequest", "PresetRemoveResponse",
                     "vc.widget.preset.remove", "VcWidgetPresetRemove", True)

    print("Merging applyPreset...")
    merge_identical(doc, "preset.apply", "ApplyPresetRequest", "ApplyPresetResponse",
                     "vc.widget.preset.apply", "VcWidgetPresetApply", False)

    print("Merging preset.add...")
    merge_preset_add(doc)

    new_keys = doc.pop("channels_new_keys")
    doc_out = {"messages": doc["messages"], "schemas": doc["schemas"], "operations": doc["operations"]}
    FRAG.write_text(yaml.safe_dump(doc_out, sort_keys=False, allow_unicode=True, width=100), encoding="utf-8")

    print(f"\nWrote {FRAG}")
    print(f"New channel message keys needed at merge time (merge.py generates these automatically "
          f"from component message keys, this list is just for confirmation): {[k for k, _ in new_keys]}")


if __name__ == "__main__":
    main()
