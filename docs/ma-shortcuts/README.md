# MA-style keyboard shortcuts — feature tracker

**Status: design complete, implementation not started.** This is a deliberately large
feature the user wants to pick up in its own session rather than folding into
whatever else is in flight. This file is the entry point for that session.

## Goal

Add a comprehensive keyboard-shortcut system to qmlui covering "almost every UI
button", explicitly modeled on how MA lighting consoles (grandMA2/grandMA3) handle
keyboard-driven workflows — the user's own framing: "I bet they have a well-thought-out
design." Not a literal console port (QLC+ has no hardware faders/encoders/softkey
strips) — the design adapts MA's *structure* to a keyboard+mouse desktop app.

## Full design doc

**`docs/agent-reports/2026-08-29-ma-style-shortcuts-design.md`** — read this in full
before starting. It contains the MA research (grounded in the official grandMA2 onPC
keyboard reference, high confidence; grandMA3's exact default letter-map is medium
confidence since the primary source was image-only/unparseable), the full ~50-action
Tier-1 shortcut table, the modifier-key grammar, the architecture (`ShortcutManager`
context property, text-field focus guard, migration of existing hardcoded bindings,
arbitration with Virtual Console's existing per-widget keybind system), and the
6-phase implementation plan with per-phase scope/effort.

## Design philosophy (one paragraph, see the full doc for the rest)

Mirrors MA3's own two-tier structure: an always-on Tier 1 of desktop-standard
shortcuts (Ctrl/Shift/Alt grammar, MA-derived touches like Space=transport,
PgUp/PgDn=page/universe, staged Escape=Clear) across Global/Fixtures&Functions/
VC/Simple Desk/Show Manager — and an optional, explicitly-indicated Tier 2 later
(MA2-style single-letter "console mode" plus a `1 thru 10 enter`-style selection
command line), gated the same way MA itself gates its console layer (opt-in,
visibly indicated, auto-suppressed while a text field has focus).

## The 6 phases (each independently shippable — see the design doc for exact scope)

1. **Core + bug fix.** `ShortcutManager` registry, the text-field focus guard,
   migration of the 7 existing hardcoded shortcuts, global keys (tab switching, etc).
   **Ships a real, already-confirmed bug fix as a side effect**: Delete/Ctrl+A
   currently fire while typing in rename/search fields — there is no focus guard
   today. This alone may be worth doing early even if the rest of the feature stalls.
2. Fixtures & Functions shortcuts.
3. Virtual Console + Show Manager shortcuts, including reconciling with VC's
   existing per-widget input-binding system (which stays tab-global and wins on
   conflict — don't let the new global layer shadow it).
4. Simple Desk shortcuts + discoverability (binding-suffixed tooltips, F1
   cheat-sheet overlay as the desktop equivalent of MA's ShCuts indicator).
5. A remapping/customization editor (JSON persistence, import/export, à la
   `UiManager`'s existing settings pattern).
6. Optional Tier-2 console-mode letter layer + command line — explicitly last,
   explicitly optional, revisit only if Tiers 1 lands well and there's appetite.

## How to resume this in a fresh session

1. Read the full design doc (link above) end to end.
2. Confirm it's still current — check `git log` for anything touching keyboard
   shortcuts, `qmlui/contextmanager.cpp`'s `handleKeyPress()`, or VC's input-binding
   code since 2026-08-29, in case something shipped in the meantime that the design
   doc doesn't know about.
3. Dispatch Phase 1 as a worktree-isolated implementation agent, following this
   project's standing workflow: the agent investigates, implements, compiles via
   plain `cmake --build` (never `dev-build-run.ps1`), commits, and reports back;
   review its diff with a three-dot `git diff master...branch`, merge, rebuild, and
   only run/deploy after asking the user first.
4. Proceed phase by phase — each phase's own report should flag anything in the
   design doc that turned out to be wrong or needs adjusting for the next phase,
   the same way this session's other multi-phase work (the AsyncAPI spec fixes)
   was tracked.

## Context this session had that a fresh one won't

- This design was produced by a Fable-model read-only research agent per the
  user's explicit request to keep the scheme "similar to MA's layout."
- No implementation of any phase has happened yet as of 2026-08-29 — this really
  is a clean start.
- The user is receptive to phased delivery and has consistently preferred
  reviewing/testing incrementally rather than one giant change.
