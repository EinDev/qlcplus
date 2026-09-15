#!/usr/bin/env python3
"""Snapshot the DMX state QLC+ is currently transmitting into a new Scene.

Reads every universe through the control API (io.dmx.universe.get - the real
values on the wire, including latched LTP channels), maps each patched
fixture's channels to those values, creates a Scene and writes the full value
list into it (functions.create + functions.scene.setValues). Channels not
covered by any fixture cannot live in a Scene and are listed at the end.

Usage (mingw64 python, QLC+ running with --api):
  /c/msys64/mingw64/bin/python3.exe dev_snapshot_scene.py [--name "Snapshot ..."] [--path Snapshots]
      [--only-nonzero] [--dump snapshot.json] [--dry-run]
"""
import argparse
import json
import sys
import time
import uuid

from dev_ws_client import DEFAULT_URI, QlcApiError, QlcClient, connect


def hello_revision(uri: str, timeout: float) -> int:
    """QlcClient discards the hello result; do one hello by hand for docRevision."""
    ws = connect(uri, open_timeout=timeout, legacy=True)
    try:
        req_id = "py-" + uuid.uuid4().hex[:8]
        ws.send(json.dumps({"type": "request", "id": req_id, "method": "hello", "params": {}}))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = json.loads(ws.recv(timeout=timeout))
            if data.get("type") == "response" and data.get("id") == req_id:
                if not data.get("ok"):
                    raise QlcApiError("hello", data.get("error"))
                return int(data["result"]["docRevision"])
        raise TimeoutError("no hello response")
    finally:
        ws.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default=time.strftime("Snapshot %Y-%m-%d %H:%M:%S"))
    ap.add_argument("--path", default="Snapshots")
    ap.add_argument("--only-nonzero", action="store_true", help="store only channels that are non-zero")
    ap.add_argument("--dump", default=None, help="also write the raw universes + values to this JSON file")
    ap.add_argument("--dry-run", action="store_true", help="read and report, create nothing")
    ap.add_argument("--from-dump", default=None,
                    help="use the universes captured by dev_artnet_dump.py (ArtNet universe N -> QLC+ universe N+1) "
                         "instead of asking QLC+ for its own output")
    ap.add_argument("--uri", default=DEFAULT_URI)
    args = ap.parse_args()

    with QlcClient(args.uri, timeout=10.0) as q:
        # 1. universes on the wire
        universes = []
        if args.from_dump:
            with open(args.from_dump, encoding="utf-8") as f:
                dump = json.load(f)
            count = max(int(k) for k in dump["universes"]) + 1 if dump["universes"] else 0
            universes = [[0] * 512 for _ in range(count)]
            for k, frame in dump["universes"].items():
                universes[int(k)] = list(frame)
            print("using ArtNet dump from", dump.get("captured_at"), "(%d packets)" % dump.get("packets", 0))
        else:
            u = 0
            while True:
                try:
                    universes.append(q.get_dmx_universe(u))
                except QlcApiError as e:
                    if isinstance(e.error, dict) and e.error.get("code") == "NOT_FOUND":
                        break
                    raise
                u += 1

        # 2. patched fixtures -> values map "<fixtureId>.<channel>": value
        fixtures = q.list_fixtures()
        values = {}
        covered = [bytearray(512) for _ in universes]
        for fx in fixtures:
            uni, addr, nch = fx["universe"], fx["address"], fx["channels"]
            if uni >= len(universes):
                continue
            frame = universes[uni]
            for ch in range(nch):
                a = addr + ch
                if a >= 512:
                    break
                covered[uni][a] = 1
                v = frame[a]
                if args.only_nonzero and v == 0:
                    continue
                values["%s.%d" % (fx["id"], ch)] = v

        unpatched = []
        for ui, frame in enumerate(universes):
            for a, v in enumerate(frame):
                if v and not covered[ui][a]:
                    unpatched.append((ui + 1, a + 1, v))

        nonzero_total = sum(1 for f in universes for v in f if v)
        print("universes: %d, fixtures: %d, non-zero channels on the wire: %d, scene entries: %d"
              % (len(universes), len(fixtures), nonzero_total, len(values)))
        if unpatched:
            print("non-zero channels with no fixture (NOT stored): "
                  + " ".join("U%d:%d=%d" % t for t in unpatched[:40]) + (" ..." if len(unpatched) > 40 else ""))

        if args.dump:
            with open(args.dump, "w", encoding="utf-8") as f:
                json.dump({"captured_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
                           "universes": universes, "values": values,
                           "unpatched": unpatched}, f)
            print("dump written:", args.dump)

        if args.dry_run:
            return 0

        # 3. create the Scene and fill it
        rev = hello_revision(args.uri, 10.0)
        created = q.call("functions.create", {"type": "Scene", "name": args.name,
                                               "path": args.path, "baseRevision": rev})
        fid, rev = created["functionId"], created["docRevision"]
        res = q.call("functions.scene.setValues", {"functionId": fid, "values": values,
                                                   "baseRevision": rev}, timeout=30.0)
        print("created Scene '%s' (id %s) under '%s' with %d values; docRevision now %s"
              % (args.name, fid, args.path, len(values), res.get("docRevision")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
