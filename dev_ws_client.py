#!/usr/bin/env python3
"""Control API WebSocket client for a running qlcplus5 -Api instance.

Library usage — hold ONE connection and make many calls, instead of
reconnecting (and re-running the mandatory "hello" handshake) per call:

    from dev_ws_client import QlcClient

    with QlcClient() as qlc:
        fns = qlc.list_functions(path_filter="Hardstyle/Gobo Spot")
        for f in fns:
            qlc.start_function(f["id"])
        time.sleep(0.5)
        dmx = qlc.get_dmx_universe(0)   # real, currently-transmitted values

CLI usage — one call, prints the JSON result to stdout:

    python3 dev_ws_client.py functions.list '{"pathFilter":"Hardstyle"}'
    python3 dev_ws_client.py functions.start '{"functionId":"2044"}'

Run with the MSYS2 mingw64 python3 (has the `websockets` package installed
via `pacman -S mingw-w64-x86_64-python-websockets`), not the Microsoft
Store python3 alias:

    /c/msys64/mingw64/bin/python3.exe dev_ws_client.py functions.list

Every session must send "hello" before anything else is accepted
(ApiServer::registerSessionMethods, UNAUTHORIZED otherwise) - QlcClient
does this automatically in its constructor.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
import uuid
from typing import Any, Optional

from websockets.sync.client import connect

DEFAULT_URI = "ws://127.0.0.1:9010"


class QlcApiError(RuntimeError):
    def __init__(self, method: str, error: Any):
        super().__init__(f"Qlc API error for '{method}': {error}")
        self.method = method
        self.error = error


class QlcClient:
    """One WebSocket connection to the Control API, already past hello."""

    def __init__(self, uri: str = DEFAULT_URI, timeout: float = 5.0):
        self.timeout = timeout
        self.ws = connect(uri, open_timeout=timeout, legacy=True)
        self.call("hello", {})

    def __enter__(self) -> "QlcClient":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def close(self) -> None:
        self.ws.close()

    def call(self, method: str, params: Optional[dict] = None, timeout: Optional[float] = None) -> Any:
        """Send one request, return its result. Raises QlcApiError on an
        ErrorResponse, TimeoutError if no matching response arrives in time.
        Broadcast events seen while waiting are silently skipped — use
        listen_events() if you need to observe them."""
        req_id = "py-" + uuid.uuid4().hex[:8]
        self.ws.send(json.dumps({"type": "request", "id": req_id, "method": method, "params": params or {}}))
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"No response for '{method}' within {self.timeout}s")
            try:
                msg = self.ws.recv(timeout=remaining)
            except TimeoutError:
                raise TimeoutError(f"No response for '{method}' within {self.timeout}s")
            data = json.loads(msg)
            if data.get("type") == "response" and data.get("id") == req_id:
                if not data.get("ok"):
                    raise QlcApiError(method, data.get("error"))
                return data.get("result")
            # else: an unrelated broadcast event arrived first - ignore and keep waiting

    def call_batch(self, calls: list[tuple[str, dict]], timeout: Optional[float] = None) -> list[Any]:
        """Pipelined version of call(): sends every (method, params) pair
        WITHOUT waiting for each ack individually, then drains all responses.
        One animation-loop frame of e.g. 128 io.simpleDesk.setChannel calls
        is ~128 sequential round-trips with call() (measured: visibly
        stuttery well below the intended frame rate) vs. one round-trip's
        worth of latency total here. Returns results in the same order as
        `calls`; raises QlcApiError on the first ErrorResponse encountered."""
        ids = ["py-" + uuid.uuid4().hex[:8] for _ in calls]
        for req_id, (method, params) in zip(ids, calls):
            self.ws.send(json.dumps({"type": "request", "id": req_id, "method": method, "params": params or {}}))
        pending = dict(zip(ids, calls))
        results: dict[str, Any] = {}
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        while pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"No response for {len(pending)}/{len(calls)} batched call(s) within {self.timeout}s")
            try:
                msg = self.ws.recv(timeout=remaining)
            except TimeoutError:
                raise TimeoutError(f"No response for {len(pending)}/{len(calls)} batched call(s) within {self.timeout}s")
            data = json.loads(msg)
            req_id = data.get("id")
            if data.get("type") == "response" and req_id in pending:
                method = pending.pop(req_id)[0]
                if not data.get("ok"):
                    raise QlcApiError(method, data.get("error"))
                results[req_id] = data.get("result")
            # else: an unrelated broadcast event, or a response to something else - ignore
        return [results[req_id] for req_id in ids]

    def listen_events(self, seconds: float, topics: Optional[list[str]] = None) -> list[dict]:
        """Optionally subscribe to `topics`, then collect broadcast events
        for `seconds`. Useful for watching functions.status.changed / core.log
        while something plays."""
        if topics:
            self.call("subscribe", {"topics": topics})
        events = []
        deadline = time.monotonic() + seconds
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                msg = self.ws.recv(timeout=remaining)
            except TimeoutError:
                break
            data = json.loads(msg)
            if data.get("type") == "event":
                events.append(data)
        return events

    # --- Thin convenience wrappers for the calls scripts need most often ---

    def list_functions(self, path_filter: Optional[str] = None, type_filter: Optional[list[str]] = None) -> list[dict]:
        params: dict = {}
        if path_filter is not None:
            params["pathFilter"] = path_filter
        if type_filter is not None:
            params["typeFilter"] = type_filter
        return self.call("functions.list", params, timeout=8)["functions"]

    def start_function(self, function_id: str | int, **overrides) -> dict:
        return self.call("functions.start", {"functionId": str(function_id), **overrides})

    def stop_function(self, function_id: str | int, preserve_attributes: bool = False) -> dict:
        return self.call("functions.stop", {"functionId": str(function_id), "preserveAttributes": preserve_attributes})

    def list_fixtures(self, universe: Optional[int] = None) -> list[dict]:
        params = {"universe": universe} if universe is not None else {}
        return self.call("fixtures.list", params, timeout=8)["fixtures"]

    def set_channels(self, addr_value_pairs: list[tuple[int, int]], timeout: Optional[float] = None) -> None:
        """io.simpleDesk.setChannels: one WebSocket message for the whole
        batch of (absoluteAddress, value) pairs, instead of one message per
        channel. Use this for anything animating more than a handful of
        channels per frame - measured live: cut per-frame call time from an
        avg 12.4ms (many small messages) to well under that with one bulk
        message, on top of the earlier delta-write reduction in writes/frame."""
        if not addr_value_pairs:
            return
        self.call("io.simpleDesk.setChannels", {"channels": [{"address": a, "value": v} for a, v in addr_value_pairs]}, timeout=timeout)

    def get_fixture(self, fixture_id: str | int) -> dict:
        return self.call("fixtures.get", {"fixtureId": str(fixture_id)})

    def get_dmx_universe(self, universe_id: int) -> list[int]:
        """Real, currently-transmitted (post-Grand-Master) DMX values for one
        universe - 512 ints, index N = DMX address N (0-based, matches a
        fixture's own "address" field). This is the blind-verification read:
        after start_function(), read this back and compare against the
        expected channel values instead of assuming the trigger worked."""
        return self.call("io.dmx.universe.get", {"universeId": universe_id})["values"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("method", help='API method, e.g. "functions.list"')
    parser.add_argument("params", nargs="?", default="{}", help="JSON object for the request params")
    parser.add_argument("--uri", default=DEFAULT_URI)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    try:
        params = json.loads(args.params)
    except json.JSONDecodeError as e:
        print(f"Invalid --params JSON: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        with QlcClient(uri=args.uri, timeout=args.timeout) as qlc:
            result = qlc.call(args.method, params, timeout=args.timeout)
            print(json.dumps(result, indent=2))
    except (QlcApiError, TimeoutError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
