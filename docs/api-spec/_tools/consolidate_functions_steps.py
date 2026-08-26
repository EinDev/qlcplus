#!/usr/bin/env python3
"""
Chaser and Sequence (Sequence subclasses Chaser's runner) each independently
defined a step-list CRUD: functions.chaser.addStep/removeStep/replaceStep/
moveStep and functions.sequence.addStep/removeStep/replaceStep/moveStep.
Verified by hand (not assumed) that:

  - removeStep: identical params {functionId, index, baseRevision} (same
    required list, same property types) across both types.
  - moveStep: identical params {functionId, sourceIndex, destIndex,
    baseRevision} across both types.
  - Both already resolved to the *same-shaped* OkResponse ({docRevision},
    via $ref FunctionsDocRevisionResult) even though Chaser and Sequence
    each had their own separately-named OkResponse message
    (FunctionsChaserStepsMutationOkResponse vs.
    FunctionsSequenceStepsMutationOkResponse) - folded into one shared
    FunctionsStepsMutationOkResponse.
  - addStep/replaceStep: params identical EXCEPT the `step` field's schema
    ref - FunctionsChaserStep (has targetFunctionId, no values) vs.
    FunctionsSequenceStep (no targetFunctionId - every step implicitly
    targets the Sequence's boundSceneId - but has a `values` field Chaser's
    step doesn't). Read both schemas in full: this is real semantic
    variance, not a coincidence, so these two get the same discriminated-
    union treatment as functions.get's typeDetail and vc.widget.preset.add's
    `preset` field: params.step: oneOf[FunctionsChaserStep,
    FunctionsSequenceStep], discriminated implicitly by functionId's own
    type (looked up server-side - no separate discriminator field, same
    pattern as vc.widget.preset.add's widgetId).

Collapses 4 method-pairs -> 4 generic methods (functions.chaser.X +
functions.sequence.X -> functions.steps.X for X in
addStep/removeStep/replaceStep/moveStep), and folds the two per-type
step-mutation OkResponse messages into one shared
FunctionsStepsMutationOkResponse (also redirecting Sequence's
applyDumpValues, which has no Chaser equivalent and stays a separate,
unmerged operation, to reuse it - its result shape was already identical).

Explicitly NOT merged (checked, found genuinely asymmetric or too divergent
to be a redundant read/write of the same conceptual object):
  - functions.sequence.applyDumpValues has no Chaser equivalent (Chaser
    steps target arbitrary functions; only a Sequence step captures live
    DMX values against its bound Scene) - left as its own method.
  - functions.chaser.setSpeedModes / functions.chaser.changed /
    functions.chaser.setAction have no functions.sequence.* counterpart in
    the spec at all - setAction's own description already documents that it
    also drives a running Sequence (shared runtime method under the
    chaser.* name, not a duplicate to remove).
  - FunctionsChaserStepsChangedEvent / FunctionsSequenceStepsChangedEvent
    are left as distinct topics (functions.chaser.stepsChanged /
    functions.sequence.stepsChanged) even though their `data` shape is
    identical - same call already made for the VC preset consolidation's
    presetsChanged events: topic distinctness reads better for a human
    filtering logs/subscriptions than the 2-message saving is worth, so not
    forced.
  - functions.audio.setSource vs functions.video.setSource (and their
    listCapabilities/sourceChanged siblings): superficially similar
    (functionId + one string field + baseRevision) but checked field-by-
    field - listCapabilities' results differ completely (Audio:
    {extensions}; Video: {videoExtensions, pictureExtensions, screens}), and
    sourceChanged's events differ substantially (Video's carries
    isPicture/detectedResolution/videoCodec/audioCodec that Audio's has no
    equivalent of). Real per-type variance, not superficial name-rhyming, so
    left as separate methods.

Modifies functions-core.yaml in place.
"""
from pathlib import Path
import yaml

FRAG = Path(__file__).resolve().parent.parent / "fragments" / "functions-core.yaml"

TYPES = ["Chaser", "Sequence"]

SHARED_OK_RESPONSE = "FunctionsStepsMutationOkResponse"


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


def add_ops(operations, req_key, resp_key):
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


def new_response_message():
    return {"payload": {
        "oneOf": [
            {"$ref": f"#/components/messages/{SHARED_OK_RESPONSE}/payload"},
            {"$ref": "#/components/messages/ErrorResponse/payload"},
        ]
    }}


def add_shared_ok_response(doc):
    messages = doc["messages"]
    template = messages[f"Functions{TYPES[0]}StepsMutationOkResponse"]
    messages[SHARED_OK_RESPONSE] = {"payload": dict(template["payload"])}
    print(f"Added shared {SHARED_OK_RESPONSE} (same shape as the two per-type ones it replaces)")


def merge_identical(doc, request_suffix, new_method, new_prefix):
    """removeStep / moveStep: params identical across Chaser and Sequence -
    no discriminated union needed. Keeps Chaser's Request params as the
    template (byte-identical to Sequence's, verified before calling this)."""
    messages, operations = doc["messages"], doc["operations"]

    template_key = f"Functions{TYPES[0]}{request_suffix}Request"
    template = messages[template_key]

    removed_msgs, removed_ops = [], []
    for t in TYPES:
        for suffix in (f"{request_suffix}Request", f"{request_suffix}Response"):
            key = f"Functions{t}{suffix}"
            if key in messages:
                removed_msgs.append(key)
                removed_ops += remove_message_and_ops(messages, operations, key)

    new_req_key = f"{new_prefix}Request"
    new_resp_key = f"{new_prefix}Response"

    messages[new_req_key] = {"payload": {
        "type": "object",
        "required": ["type", "id", "method", "params"],
        "properties": {
            "type": {"const": "request"},
            "id": {"type": "string"},
            "method": {"const": new_method},
            "params": template["payload"]["properties"]["params"],
        },
    }}
    messages[new_resp_key] = new_response_message()

    add_ops(operations, new_req_key, new_resp_key)
    print(f"  {request_suffix}: removed {removed_msgs} + ops {removed_ops}; added {new_req_key}, {new_resp_key}")


def merge_step_union(doc, request_suffix, new_method, new_prefix, index_required):
    """addStep / replaceStep: params identical except `step`'s $ref -
    FunctionsChaserStep vs FunctionsSequenceStep (real per-type variance,
    see module docstring) -> discriminated union on `step`, discriminated
    implicitly by functionId's own type, server-side."""
    messages, operations = doc["messages"], doc["operations"]

    chaser_req_key = f"Functions{TYPES[0]}{request_suffix}Request"
    chaser_params = messages[chaser_req_key]["payload"]["properties"]["params"]
    index_field = chaser_params["properties"]["index"]

    removed_msgs, removed_ops = [], []
    for t in TYPES:
        for suffix in (f"{request_suffix}Request", f"{request_suffix}Response"):
            key = f"Functions{t}{suffix}"
            if key in messages:
                removed_msgs.append(key)
                removed_ops += remove_message_and_ops(messages, operations, key)

    new_req_key = f"{new_prefix}Request"
    new_resp_key = f"{new_prefix}Response"

    step_field = {
        "oneOf": [
            {"$ref": "#/components/schemas/FunctionsChaserStep"},
            {"$ref": "#/components/schemas/FunctionsSequenceStep"},
        ],
    }
    description = (
        "params.step's shape is determined by the target function's own type "
        "(Chaser or Sequence, looked up server-side from functionId, not a "
        "separate discriminator field here) - see FunctionsChaserStep, "
        "FunctionsSequenceStep."
    )

    if index_required:
        # replaceStep: functionId, index, step, baseRevision (index is which
        # step to replace, always required).
        properties = {
            "functionId": {"type": "string"},
            "index": dict(index_field),
            "step": step_field,
            "baseRevision": {"type": "integer"},
        }
        required = ["functionId", "index", "step", "baseRevision"]
    else:
        # addStep: functionId, step, index, baseRevision (index is an
        # optional insertion index; omit/-1 to append).
        properties = {
            "functionId": {"type": "string"},
            "step": step_field,
            "index": dict(index_field),
            "baseRevision": {"type": "integer"},
        }
        required = ["functionId", "step", "baseRevision"]

    messages[new_req_key] = {"payload": {
        "type": "object",
        "required": ["type", "id", "method", "params"],
        "properties": {
            "type": {"const": "request"},
            "id": {"type": "string"},
            "method": {"const": new_method},
            "params": {
                "type": "object",
                "required": required,
                "description": description,
                "properties": properties,
            },
        },
    }}
    messages[new_resp_key] = new_response_message()

    add_ops(operations, new_req_key, new_resp_key)
    print(f"  {request_suffix}: removed {removed_msgs} + ops {removed_ops}; added {new_req_key}, {new_resp_key}")


def redirect_apply_dump_values_and_cleanup(doc):
    messages = doc["messages"]

    resp = messages["FunctionsSequenceApplyDumpValuesResponse"]
    old_ref = f"#/components/messages/Functions{TYPES[1]}StepsMutationOkResponse/payload"
    new_ref = f"#/components/messages/{SHARED_OK_RESPONSE}/payload"
    one_of = resp["payload"]["oneOf"]
    for entry in one_of:
        if entry.get("$ref") == old_ref:
            entry["$ref"] = new_ref
    print(f"Redirected FunctionsSequenceApplyDumpValuesResponse -> {SHARED_OK_RESPONSE}")

    for t in TYPES:
        key = f"Functions{t}StepsMutationOkResponse"
        if key in messages:
            del messages[key]
            print(f"Removed now-unreferenced {key}")


def main():
    doc = load()

    add_shared_ok_response(doc)

    print("Merging removeStep...")
    merge_identical(doc, "RemoveStep", "functions.steps.removeStep", "FunctionsStepsRemoveStep")

    print("Merging moveStep...")
    merge_identical(doc, "MoveStep", "functions.steps.moveStep", "FunctionsStepsMoveStep")

    print("Merging addStep...")
    merge_step_union(doc, "AddStep", "functions.steps.addStep", "FunctionsStepsAddStep", index_required=False)

    print("Merging replaceStep...")
    merge_step_union(doc, "ReplaceStep", "functions.steps.replaceStep", "FunctionsStepsReplaceStep", index_required=True)

    redirect_apply_dump_values_and_cleanup(doc)

    doc_out = {"messages": doc["messages"], "schemas": doc["schemas"], "operations": doc["operations"]}
    FRAG.write_text(yaml.safe_dump(doc_out, sort_keys=False, allow_unicode=True, width=100), encoding="utf-8")
    print(f"\nWrote {FRAG}")


if __name__ == "__main__":
    main()
