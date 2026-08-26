# Merge plan — notes before executing the final merge

Repo owner's explicit guidance (verbatim intent): expect a lot of cross-
domain overlap; merge similar requests into one rather than preserving every
domain's bespoke shape; fewer methods overall makes the eventual client
codebase easier to read. This file is the running plan for where that
applies, checked against the actual fragment content (not assumed) before
being written down.

## Confirmed consolidation #1: collapse `functions.<type>.get` into `functions.get`

**Finding** (verified by reading `functions-core.yaml` directly): the
generic `FunctionsGetOkResponse.result` is `FunctionsDetail` — deliberately
base-only (id/name/type/path/runOrder/direction/tempoType/fadeInSpeed/
fadeOutSpeed/duration/totalDuration/blendMode/attributes), with an explicit
comment: *"Type-specific detail (values/steps/geometry/members) comes from
the matching functions.<type>.get call."* Each of the 10 function types then
has its own `functions.<type>.get` (`FunctionsSceneDetail`,
`FunctionsChaserDetail`, etc. — the extension-only fields: e.g. Scene's
`values`/`fixtures`/`fixtureGroups`/`channelGroups`/`palettes`).

This is clean base/extension field separation (no field-level duplication),
but it forces two round-trips to load one function's full picture. Fix:
have the single `functions.get` response embed the type-specific detail
directly — e.g. `result: { ...FunctionsDetail fields..., typeDetail: {
oneOf: [FunctionsSceneDetail, FunctionsChaserDetail, ...] } }`, discriminated
by the already-present `type` field. This removes, outright:

- `functions.scene.get`, `functions.chaser.get`, `functions.efx.get`,
  `functions.collection.get`, `functions.sequence.get` (functions-core.yaml)
- `functions.script.get`, `functions.rgbmatrix.get`, `functions.show.get`,
  `functions.audio.get`, `functions.video.get` (functions-advanced.yaml)

...and their paired `*GetResponse`/`*GetOkResponse` messages and `operations`
entries — roughly 9-10 request/response message pairs removed, replaced by
richer output from the one call that already exists. `functions.list`
correctly stays summary-only (`FunctionsSummary`, lighter weight, used for
list/tree views) — that's normal list-vs-detail API shape, not the kind of
duplication being targeted here; no change needed there.

## Where NOT to over-consolidate

Distinct *mutations* per type (`functions.scene.setValues`,
`functions.chaser.addStep`/`moveStep`/`removeStep`/`replaceStep`,
`functions.efx.setParameters`, etc.) should stay separate, granular
methods — collapsing these into one generic "functions.setConfig(partial
blob)" would (a) lose type safety/schema validation per-operation, which is
half the point of using AsyncAPI/JSON-Schema at all, and (b) work against
the §4a optimistic-concurrency model: smaller, purpose-specific mutations
mean smaller conflict surface (two clients editing different scene channels
concurrently shouldn't conflict; they would more often if forced through
one coarse "replace the whole config" method). The target for consolidation
is redundant **reads of the same conceptual object**, not writes.

## Still to check once all fragments are in

- **Virtual Console** (pending the retry agent): 11 widget types is the
  single most repetitive domain by construction. Check for the identical
  `vc.<type>.get` vs generic `vc.widget.get` split pattern found in
  functions-core/advanced, and apply the same fix if present. The original
  task prompt for that domain *did* ask for a shared `vc.widget.*` base for
  structural CRUD, so this may already be handled correctly — verify rather
  than assume, the same way the functions.list summary-vs-detail split
  turned out to already be correct on inspection.
- **Cross-domain "own revision" scheme naming**: `fixturedefs.yaml` and
  `io.yaml` (input profiles) both independently invented a per-domain
  revision counter (`defRevision`/`sessionRevision` vs `profilesRevision`)
  for the same underlying problem (shared library resource, not part of the
  per-show docRevision). Not a method-count issue, but worth unifying the
  *concept* in the merged spec's shared vocabulary/glossary section so it
  reads as one deliberate pattern applied twice, not two ad hoc inventions
  that happen to coincide.
- **Skim for smaller name-only inconsistencies** across all domains once
  merged into one file (e.g. `.rename` vs `.setName`, `.delete` vs
  `.remove`) and normalize to one verb choice repo-wide before finalizing -
  cheap, high-readability-payoff pass, do it last once everything is in one
  document and greppable together.
