# fixturedefs domain — notes

> Reconstructed by the coordinator, not the original authoring agent: that
> agent's run was killed by an account spend-limit error right after writing
> `fixturedefs.yaml` but before writing this file. This is a summary derived
> from reading the finished YAML and its inline comments.

## Scope

Fixture *definition* authoring only (`fixturedefs.*`: channel/capability/
mode/head/alias CRUD, physical properties, save/export) — explicitly not
patching a definition onto a real fixture (that's `fixtures.*`, a different
domain). Mirrors `qmlui/fixtureeditor/`.

## The docRevision-vs-own-revision decision (this domain's big call)

Fixture definitions are a shared library resource reused across shows, not
part of one project's `.qxw`, so `fixturedefs.yaml` does **not** gate its
mutations on the show's global `docRevision` from 00-conventions.md §4a.
Instead it introduces a two-tier scheme, visible in the schemas:

- **`defRevision`** — per-definition (manufacturer+model) revision in the
  shared library cache. `fixturedefs.delete` and `fixturedefs.save` take a
  `baseRevision` (referring to this `defRevision`) to detect if the library
  copy changed since the client last looked.
- **`sessionRevision`** — an in-memory editing session (`fixturedefs.session.*`
  — create/open/close/import/list/forkToUser/setMetadata/setPhysical/
  validate) is a private draft cloned from the library, starting its own
  revision counter at 0. All the channel/mode/capability/alias mutation
  methods operate against a `sessionId` and a `baseRevision` (referring
  to this `sessionRevision`).
- There's also a `forkToUser` method (a "Save As" for definitions, likely
  writing to the user's fixture directory rather than the system one, per
  QLC+'s usual manufacturer/model dual-lookup path) - confirm this
  interpretation against `qlcfixturedefcache.h`'s system-vs-user directory
  handling if it matters for the merged spec.

## Other flags for the merge pass

- No topics in this fragment look like they need subscribe-gating (§5) -
  definition editing isn't a high-frequency live-data domain like DMX.
- Lockable resources: a `sessionId` (one user editing one definition) is the
  natural advisory-lock (§6) target here, not the raw manufacturer/model -
  confirm the merge pass wires `locks.acquire` examples/docs accordingly.
- `fixturedefs.channel.capability.alias.*` and `.autoPatchColors` look like
  QLC+-specific conveniences worth a one-line explanation in the merged
  spec's prose (aliasing = reusing one capability set across near-identical
  channels; autoPatchColors likely bulk-assigns color-preset capabilities
  from a colour-wheel definition) - verify against `qlccapability.h`/
  `qmlui/fixtureeditor/aliasedit.cpp` before writing that prose.
