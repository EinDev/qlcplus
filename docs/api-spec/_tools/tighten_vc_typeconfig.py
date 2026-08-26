#!/usr/bin/env python3
"""
VcWidgetDetail.typeConfig is currently `{ type: object }` - accepts
anything, untyped. All 10 non-Label Vc<Type>Config schemas already exist
(VcButtonConfig, VcSliderConfig, ...). Replace the loose object with a
proper oneOf over them, so a single vc.widget.get is both consolidated
AND strongly typed - matching the functions.get consolidation's standard.
"""
from pathlib import Path
import yaml

FRAG = Path(__file__).resolve().parent.parent / "fragments" / "virtualconsole.yaml"

CONFIG_SCHEMAS = [
    "VcButtonConfig", "VcSliderConfig", "VcXyPadConfig", "VcFrameConfig",
    "VcSoloFrameConfig", "VcClockConfig", "VcCueListConfig",
    "VcSpeedDialConfig", "VcAnimationConfig", "VcAudioTriggersConfig",
]

doc = yaml.safe_load(FRAG.read_text(encoding="utf-8"))
detail = doc["schemas"]["VcWidgetDetail"]

# VcWidgetDetail is `allOf: [VcWidgetSummary, {type: object, properties: {typeConfig: ...}}]`
extra_block = next(b for b in detail["allOf"] if isinstance(b, dict) and "properties" in b)
old = extra_block["properties"]["typeConfig"]
extra_block["properties"]["typeConfig"] = {
    "description": (
        "Type-specific config, discriminated by VcWidgetSummary.type. "
        "Label's type has no dedicated Config schema (kept as an empty "
        "object, per the original {} case) - all others reference their "
        "existing Vc<Type>Config schema directly."
    ),
    "oneOf": [{"$ref": f"#/components/schemas/{name}"} for name in CONFIG_SCHEMAS] + [{"type": "object", "maxProperties": 0, "description": "Label: no type-specific config."}],
}

FRAG.write_text(yaml.safe_dump(doc, sort_keys=False, allow_unicode=True, width=100), encoding="utf-8")
print(f"VcWidgetDetail.typeConfig: {old!r} -> oneOf of {len(CONFIG_SCHEMAS)} Vc*Config schemas + Label empty-object case")
