#!/usr/bin/env python3
"""Bisect which channels of a captured DMX state produce a visible effect.

State model:
  baseline  - the DMX state to fall back to (a dev_artnet_dump.py / dev_grid_snapshot.py
              JSON); it is kept running as a Scene named "Bisect baseline".
  target    - the captured state under investigation (same JSON shape).
Overrides are applied on top of the running baseline through the Simple Desk
(io.simpleDesk.setChannels / resetChannel), which wins over any function, so
"apply" and "clear" are instant and reversible.

Commands:
  reset                      stop everything else we started, start the baseline Scene,
                             clear all overrides
  list                       fixtures whose target values differ from the baseline
  apply  <sel>               override the target values for the selection
  clear  [<sel>]             remove overrides (all, or only for the selection)
  half   <a|b>               apply the first/second half of the remaining candidate set
  keep   <a|b>               narrow the candidate set to that half (after you saw the effect)
  status                     show current candidate set and applied overrides
<sel> = comma list of fixture ids, "U6:225-242" channel ranges, or "all".
State (candidates, applied) is kept in a small JSON next to the target file so
each CLI call continues where the last one stopped.

Run with the MSYS2 mingw64 python3, QLC+ running with --api.
"""
import argparse
import json
import os
import sys
import time

from dev_ws_client import QlcApiError, QlcClient
from dev_snapshot_scene import DEFAULT_URI, hello_revision

BASELINE_SCENE = "Bisect baseline"


def load_universes(path):
    d = json.load(open(path, encoding="utf-8"))
    n = max((int(k) for k in d["universes"]), default=-1) + 1
    out = [[0] * 512 for _ in range(n)]
    for k, frame in d["universes"].items():
        out[int(k)] = list(frame)
    return out


def state_path(target):
    return os.path.splitext(target)[0] + ".bisect-state.json"


def load_state(target):
    p = state_path(target)
    if os.path.exists(p):
        return json.load(open(p, encoding="utf-8"))
    return {"candidates": None, "applied": []}


def save_state(target, st):
    json.dump(st, open(state_path(target), "w", encoding="utf-8"))


def fixture_index(q, n_universes):
    """[(fixture, [absolute addresses])] for all patched fixtures"""
    out = []
    for fx in q.list_fixtures():
        if fx["universe"] >= n_universes:
            continue
        addrs = [fx["universe"] * 512 + fx["address"] + ch for ch in range(fx["channels"])
                 if fx["address"] + ch < 512]
        out.append((fx, addrs))
    return out


def diff_groups(q, baseline, target):
    """Groups (label, [abs addresses]) where target != baseline, one per fixture,
    plus one 'unpatched U<n>' group per universe for channels no fixture covers."""
    n = min(len(baseline), len(target))
    covered = set()
    groups = []
    for fx, addrs in fixture_index(q, n):
        diff = [a for a in addrs if target[a // 512][a % 512] != baseline[a // 512][a % 512]]
        covered.update(addrs)
        if diff:
            groups.append(("fx%s %s [U%d:%d]" % (fx["id"], fx["name"], fx["universe"] + 1, fx["address"] + 1), diff))
    for u in range(n):
        diff = [u * 512 + c for c in range(512)
                if (u * 512 + c) not in covered and target[u][c] != baseline[u][c]]
        if diff:
            groups.append(("unpatched U%d" % (u + 1), diff))
    return groups


def parse_selection(sel, groups):
    if sel == "all":
        return [a for _, addrs in groups for a in addrs]
    addrs = []
    for part in sel.split(","):
        part = part.strip()
        if part.upper().startswith("U") and ":" in part:
            u, rng = part[1:].split(":")
            lo, _, hi = rng.partition("-")
            lo = int(lo); hi = int(hi) if hi else lo
            addrs += [(int(u) - 1) * 512 + c - 1 for c in range(lo, hi + 1)]
        else:
            lo, _, hi = part.partition("-")
            lo = int(lo); hi = int(hi) if hi else lo
            wanted = set("fx%d " % i for i in range(lo, hi + 1))
            for label, ga in groups:
                if any(label.startswith(w) for w in wanted):
                    addrs += ga
    return addrs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--target", required=True)
    ap.add_argument("command")
    ap.add_argument("arg", nargs="?")
    args = ap.parse_args()

    baseline = load_universes(args.baseline)
    target = load_universes(args.target)
    st = load_state(args.target)

    with QlcClient(timeout=15.0) as q:
        groups = diff_groups(q, baseline, target)
        if st["candidates"] is None:
            st["candidates"] = [g[0] for g in groups]
        by_label = dict(groups)

        def apply(addrs):
            q.set_channels([(a, target[a // 512][a % 512]) for a in addrs])
            st["applied"] = sorted(set(st["applied"]) | set(addrs))

        def clear(addrs=None):
            todo = st["applied"] if addrs is None else [a for a in addrs if a in st["applied"]]
            for a in todo:
                q.call("io.simpleDesk.resetChannel", {"address": a})
            st["applied"] = [a for a in st["applied"] if a not in set(todo)]

        cmd = args.command
        if cmd == "reset":
            clear()
            # (re)create the baseline scene from the baseline values, start it
            fns = [f for f in q.list_functions(path_filter="Snapshots") if f["name"] == BASELINE_SCENE]
            rev = hello_revision(DEFAULT_URI, 10.0)
            if fns:
                fid = fns[0]["id"]
            else:
                r = q.call("functions.create", {"type": "Scene", "name": BASELINE_SCENE, "path": "Snapshots", "baseRevision": rev})
                fid, rev = r["functionId"], r["docRevision"]
            values = {}
            for fx, addrs in fixture_index(q, len(baseline)):
                for ch, a in enumerate(addrs):
                    values["%s.%d" % (fx["id"], ch)] = baseline[a // 512][a % 512]
            q.call("functions.scene.setValues", {"functionId": fid, "values": values, "baseRevision": rev}, timeout=30.0)
            # stop other snapshot scenes we may have started
            for f in q.list_functions(path_filter="Snapshots"):
                if f["id"] != fid:
                    try: q.stop_function(f["id"])
                    except QlcApiError: pass
            q.stop_function(fid); time.sleep(0.3); q.start_function(fid)
            st["candidates"] = [g[0] for g in groups]
            print("baseline Scene '%s' (id %s) running with %d values; overrides cleared; %d differing groups"
                  % (BASELINE_SCENE, fid, len(values), len(groups)))
        elif cmd == "list":
            for label, addrs in groups:
                mark = "*" if label in st["candidates"] else " "
                print("%s %-48s %3d ch  e.g. %s" % (mark, label[:48], len(addrs),
                      " ".join("%d->%d" % (baseline[a // 512][a % 512], target[a // 512][a % 512]) for a in addrs[:4])))
            print("(* = still a candidate; %d groups, %d overrides applied)" % (len(st["candidates"]), len(st["applied"])))
        elif cmd == "apply":
            addrs = parse_selection(args.arg or "all", groups)
            apply(addrs); print("applied %d channel overrides" % len(addrs))
        elif cmd == "clear":
            if args.arg:
                clear(parse_selection(args.arg, groups))
            else:
                clear()
            print("overrides now: %d" % len(st["applied"]))
        elif cmd in ("half", "keep"):
            cands = st["candidates"]
            mid = (len(cands) + 1) // 2
            part = cands[:mid] if (args.arg or "a").lower() == "a" else cands[mid:]
            if cmd == "half":
                clear()
                addrs = [a for lbl in part for a in by_label.get(lbl, [])]
                apply(addrs)
                print("applied half %s: %d groups, %d channels:" % ((args.arg or "a").lower(), len(part), len(addrs)))
                for lbl in part: print("   ", lbl)
            else:
                st["candidates"] = part
                print("candidates narrowed to %d groups:" % len(part))
                for lbl in part: print("   ", lbl)
        elif cmd == "status":
            print("candidates (%d):" % len(st["candidates"]))
            for lbl in st["candidates"]: print("   ", lbl)
            print("overrides applied: %d" % len(st["applied"]))
        else:
            ap.error("unknown command " + cmd)
        save_state(args.target, st)
    return 0


if __name__ == "__main__":
    sys.exit(main())
