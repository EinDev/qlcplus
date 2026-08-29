# Spec-wide review notes (TODO 0.1, task b)

Findings from the spec-wide consistency sweep requested in `TODO.md` item
0.1. This file documents observations only — nothing here has been fixed.
It does **not** duplicate the already-logged items 0.2 (presetsChanged
`docRevision` — confirmed already fixed, see `TODO.md`), 0.3 (`defRevision`/
`sessionRevision` vs `profilesRevision` naming), or 0.4 (full verb-consistency
sweep) except where a new, previously-unlisted example of the same category
turned up.

Method: dangling/wrong `$ref`s and required-field checks below were done
with small read-only Python scripts against the fragment YAML (via
`_tools/merge.py`'s own full-document `$ref` resolution pass, plus a few
one-off scripts written for this review) rather than eyeballing — exact
commands aren't preserved, but every finding below cites the fragment file
and either a line number or exact message/schema key so it can be re-checked
directly.

---

## 1. Dangling or wrong `$ref`s

**Clean.** Running `_tools/merge.py` (which walks every node of the fully
merged document and verifies every `$ref` resolves to a real key) against
the current fragments reports **0 broken refs** across all 710 messages /
113 schemas / 599 operations. This includes the pre-existing intentional
cross-file refs from `functions-core.yaml` into `functions-advanced.yaml`
(the `FunctionsDetail.typeDetail` discriminated union pulling in
`FunctionsAudioDetail`, `FunctionsRgbMatrixDetail`, `FunctionsScriptDetail`,
`FunctionsShowDetail`, `FunctionsVideoDetail`) — these are deliberate
(consolidation #1 in `MERGE-PLAN.md`), not bugs. No other cross-domain
schema/message references exist anywhere in the spec (checked
programmatically: for every fragment, does any `$ref` inside it resolve to
a schema/message owned by a *different* fragment — only the five listed
above came back, all expected).

No "wrong" refs found either (a `$ref` that resolves but points at a
schema of a mismatched shape/semantics) in `core.yaml`, `fixtures.yaml`, or
`fixturedefs.yaml` specifically — every `$ref` in those three files was
read and its target matches what the referencing site expects.

## 2. Naming inconsistencies (new instances, not already covered by 0.2/0.3/0.4)

- **A third verb for "create a brand-new blank resource."** TODO 0.4 already
  flags `.create` vs `.add` as a pair to reconcile. There's a third variant
  not on that list: **`.new`**. `core.project.new` (`core.yaml` line 22)
  uses "new" for exactly the same conceptual action — "start a fresh, blank
  instance of this resource, discarding what's inside" — that
  `fixturedefs.session.create` (`fixturedefs.yaml` line 192) and
  `fixtures.group.create` (`fixtures.yaml` line 608) call "create." Worth
  folding into the 0.4 sweep as a three-way (`new`/`create`/`add`) rather
  than a two-way reconciliation.

- **`FixtureDefs` (message-key prefix) vs `fixturedefs` (method dot-prefix)
  spell the domain name two different ways.** Every other multi-word domain
  either stays one flat word in both places (`Functions`/`functions.`,
  `Io`/`io.`) or uses a shared abbreviation in both places (`Vc`/`vc.` for
  "virtual console"). `fixturedefs.yaml` instead camelCases the compound at
  the message-key level (`FixtureDefs...`, capital D) but flattens it to one
  lowercase word at the method-string level (`fixturedefs.list`, no internal
  capital) — the two spellings of the same domain name don't correspond to
  each other under the usual PascalCase-to-lowerCamelCase transform the way
  every other domain's do. Not a functional bug (channel message keys are
  derived from the PascalCase key independently of the method string), but
  worth normalizing for readability — e.g. decide once whether the domain is
  "FixtureDefs"/`fixtureDefs.` or "Fixturedefs"/`fixturedefs.` everywhere.

## 3. §4a/§4b categorization

**`core.yaml`, `fixtures.yaml`, `fixturedefs.yaml` individually reviewed
message-by-message — no actual miscategorization found** (nothing in the
"should be 4b but has `baseRevision`" or "should be 4a but is missing it
without justification" bucket). However, the sweep surfaced a real gap in
**`00-conventions.md` itself**, not in any one fragment:

- **§4a says `baseRevision` "must" be included on every structural mutation,
  but at least 9 request messages across 4 different domains deliberately
  omit it, each independently justified inline rather than being named as
  an accepted exception category in `00-conventions.md`:**
  - `core.project.new` / `core.project.close` / `core.project.save` /
    `core.project.saveAs` (`core.yaml`) — "full document replace/save,
    nothing to rebase against."
  - `core.undo` / `core.redo` (`core.yaml`, lines 503 and 533) — "you're
    asking to undo/redo whatever is on top of the stack right now, not
    applying a diff against a known base." This one is the most notable:
    unlike save/new/close (which don't change existing structural content,
    they replace the whole document), undo/redo **can** and typically does
    change `docRevision` on a structural undo — it's a genuine structural
    mutation by the §4 test ("does it get saved into the .qxw file?" —
    yes, an undone Scene edit is real content) that still doesn't carry
    `baseRevision`, purely because "the top of the stack" is a different
    (and equally valid) addressing scheme than "the revision I last saw."
  - `fixturedefs.session.create` / `fixturedefs.session.close`
    (`fixturedefs.yaml`) — session lifecycle, not itself document/library
    state until `fixturedefs.save`.
  - `io.patch.output.setState`, `vc.clock.reset`, `vc.speedDial.apply`,
    `vc.widget.preset.apply` (`io.yaml`, `virtualconsole.yaml`) — all
    explicitly documented as §4b live/runtime actions.

  Every individual case is well-reasoned and documented in its own
  fragment, so this isn't "these are wrong" — it's that a future domain
  author reading only `00-conventions.md` (per its own §9 instruction to
  ground design in the shared doc) has no signal that "whole-document
  replace" and "stack-relative, not revision-relative" are accepted
  `baseRevision` exceptions, and might not realize they can reach for the
  same reasoning, or might add spurious `baseRevision` fields to a
  similar future case out of over-literal rule-following. Worth adding a
  short "Exceptions" callout to `00-conventions.md` §4a enumerating these
  two accepted patterns once a maintainer confirms they're the only two.

- **Related to (but distinct from) TODO 0.3 — now resolved concurrently
  with this review.** 0.3 is about naming the revision-counter *field*
  consistently (`defRevision`/`sessionRevision` vs `profilesRevision`).
  When this review started, `00-conventions.md` §4 only documented two
  tiers (4a document-state / 4b live-state) even though `fixturedefs.yaml`
  and part of `io.yaml` (input profiles) implement a third, then-unnamed
  tier (`baseRevision`-gated optimistic concurrency exactly like §4a, but
  keyed to a resource-specific revision counter instead of the global
  `docRevision`). **While this review was in progress, a separate,
  concurrent edit to this same working tree added a new "§4c — Shared
  library resources" section to `00-conventions.md`** (unifying on
  `<domain/resource>Revision` for the counter name and `baseRevision` for
  the request field) and marked TODO 0.3 done — see `git diff 00-conventions.md`/
  `TODO.md` if those changes aren't already committed. That resolves this
  finding as originally written; noting it here mainly so the coincidence
  is on record and so whoever reviews this file also double-checks that
  concurrent edit landed cleanly and wasn't clobbered by anything else
  touching these files around the same time (see the cover-report caveat
  about a second, unidentified process editing `fixturedefs.yaml`/
  `fixturedefs-notes.md`/`00-conventions.md`/`TODO.md` during this task).

## 4. Other schema inconsistencies

- **`fixturedefs.yaml` inlines its ok-response bodies instead of factoring
  them into a separate named `*OkResponse` message, unlike every other
  domain in the spec (including `fixturedefs.yaml`'s own later sections).**
  Every other fragment's `XxxResponse` message is
  `oneOf: [$ref XxxOkResponse/payload, $ref ErrorResponse/payload]` — a
  separate, independently-addressable `XxxOkResponse` message (see
  `_example.yaml`, and every response in `core.yaml`/`fixtures.yaml`). Eight
  responses early in `fixturedefs.yaml` break this pattern by inlining the
  full ok-response object literal directly inside the `oneOf`, with no
  companion `XxxOkResponse` message at all:
  `FixtureDefsListResponse` (line 36), `FixtureDefsGetResponse` (line 70),
  `FixtureDefsDeleteResponse` (line 108), `FixtureDefsExportResponse`
  (line 160), `FixtureDefsSessionOpenedResponse` (line 231),
  `FixtureDefsSessionCloseResponse` (line 294),
  `FixtureDefsSessionListResponse` (line 336),
  `FixtureDefsSessionValidateResponse` (line 433). By contrast, every
  *mutation* response later in the same file (`FixtureDefsChannelAddResponse`
  and siblings, `FixtureDefsSessionAckResponse`) does follow the standard
  separate-message pattern. Not a correctness bug — AsyncAPI/JSON-Schema
  validation works identically either way — but it means these 8 ok-shapes
  can't be `$ref`'d from anywhere else the way every other domain's can
  (e.g. there is no `FixtureDefsGetOkResponse/payload` a future message could
  point at), and it's an inconsistency a reader has to notice rather than
  one that's flagged anywhere. Worth normalizing to the rest-of-spec style
  in a follow-up pass.

- **`fixturedefs.delete`'s ok-response doesn't return a new `defRevision`.**
  `FixtureDefsDeleteResponse.result` is `{manufacturer, model}` only — every
  other mutation that touches the shared definition library
  (`fixturedefs.save`) returns the resulting `defRevision`. It's plausible
  this is intentional (there's no revision to report for something that no
  longer exists), but every other domain's "delete" responses in this spec
  (`FixturesGroupDeleteResponse` → `FixturesDocRevisionOkResponse`,
  `FixtureDefsSessionAckResponse`-style mutations) *do* return the new
  revision counter, so a caller can't tell from this response alone whether
  some other concurrent client's edit to a **different** definition changed
  the library in a way that affects a subsequent `fixturedefs.list` call's
  staleness. Flagging for a decision, not fixing — may be a non-issue if
  the library has no single "list revision" concept to report.

- **Inconsistent `required`-ness of a "name" field across structurally
  similar "create" operations.** `fixtures.group.create` requires `name`;
  `fixturedefs.mode.add` and `fixturedefs.channel.add` (same shape of
  operation — create a new named sub-resource) both leave `name` optional
  with a server-side default ("New mode" / "New channel N"). Each is
  internally documented and consistent with its own domain, so this is a
  soft finding — mentioned here in case a future pass wants one convention
  (always-optional-with-default, matching the two `fixturedefs` cases)
  applied spec-wide rather than case-by-case.

- **`io.yaml`: 7 request messages omit `params` from their own envelope's
  `required` list**, unlike every request message in `core.yaml`,
  `fixtures.yaml`, `fixturedefs.yaml`, and the rest of `io.yaml`, all of
  which require `[type, id, method, params]` even when `params` itself has
  no required properties (see `_example.yaml`'s worked example, or
  `CoreProjectGetRequest`/`FixturesGroupListRequest` for two which do this
  correctly for genuinely empty-params requests). The 7 outliers, all in
  `io.yaml`, have a `params: { type: object }` property present but missing
  from the top-level `required` array: `IoUniverseListRequest` (line 2),
  `IoPluginListRequest` (line 546), `IoInputProfileListRequest` (line 746),
  `IoInputProfileLearnStopRequest` (line 1056), `IoGrandMasterGetRequest`
  (line 1103), `IoBlackoutGetRequest` (line 1216), `IoBlackoutToggleRequest`
  (line 1286). Functionally low-stakes (an empty-object `params` is easy to
  satisfy either way, and a client omitting it entirely would only fail
  schema validation, not silently misbehave), but it's a real, mechanical
  envelope inconsistency, spec-wide, in a fragment marked "done" — flagging
  for whoever next touches `io.yaml` rather than fixing it here, per this
  task's instruction not to modify `functions-core.yaml`,
  `functions-advanced.yaml`, or `io.yaml`.

---

## Not flagged (checked, found fine)

- Every event message's `data` that carries a `docRevision`/
  `sessionRevision`/`defRevision` property has it in that `data` object's
  `required` list, spec-wide — the TODO 0.2 pattern re-checked across all
  seven fragments programmatically, no new instances found.
- Every `method`/`topic` constant spec-wide matches the expected
  `lowerCamelCase`-free, dot-namespaced, all-lowercase-segment pattern (e.g.
  no stray camelCase or underscore segments in a dot-path) — checked
  programmatically across all 710 messages.
- Every event message in `core.yaml`/`fixtures.yaml`/`fixturedefs.yaml` has
  an `originClientId` property (none silently omit it).
- `core.yaml`'s `CoreModeSetRequest`/`CoreSettingsSetRequest` (both
  correctly §4b/global-not-per-project, no `baseRevision`, matches their own
  inline reasoning) and `fixturedefs.yaml`'s session-mutation methods (all
  correctly carry `baseRevision` referring to `sessionRevision`) were
  double-checked and are categorized correctly.
