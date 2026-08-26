#!/usr/bin/env python3
from pathlib import Path
import yaml

SPEC = Path(__file__).resolve().parent.parent / "qlcplus-api.yaml"
d = yaml.safe_load(SPEC.read_text(encoding="utf-8"))

print("asyncapi version:", d["asyncapi"])
print("messages:", len(d["components"]["messages"]))
print("schemas:", len(d["components"]["schemas"]))
print("operations:", len(d["operations"]))
print("channel message keys:", len(d["channels"]["qlcplus"]["messages"]))

td = d["components"]["schemas"]["FunctionsDetail"]["properties"]["typeDetail"]["oneOf"]
print("functions typeDetail branches:", len(td))

vc_extra = next(b for b in d["components"]["schemas"]["VcWidgetDetail"]["allOf"] if "properties" in b)
vctd = vc_extra["properties"]["typeConfig"]["oneOf"]
print("vc widget typeConfig branches:", len(vctd))

cm = d["channels"]["qlcplus"]["messages"]
print("functions.scene.get still present?", "functionsSceneGetRequest" in cm)
print("functions.get present?", "functionsGetRequest" in cm)
print("functions.channelsgroup.get still present?", "functionsChannelsGroupGetRequest" in cm)
print("vc.widget.get present?", "vcWidgetGetRequest" in cm)
print("vc.widget.inputSource.remove present?", "vcWidgetInputSourceRemoveRequest" in cm)
print("vc.widget.inputSource.delete present (should be False)?", "vcWidgetInputSourceDeleteRequest" in cm)
