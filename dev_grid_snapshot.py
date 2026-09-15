#!/usr/bin/env python3
"""Decode a Stage Flight "Binary" DMX grid from an image into universes.

Default format is MDMX ("Binary Stage Flight", the world's "MDMX Grid - LD"
region at the top-left of the 1920x1080 frame, 1840 px wide): 6 channels per
4-px column of 48 blocks, bits MSB-first down the column, 4 CRC blocks below,
bands of 208 px. A bit is 1 when the pixel at (x+1, y+1) is bright. --format
binary selects HNode's plain Binary layout (52 blocks per column, LSB-first).

Sources for the image:
  --png FILE                 any 1:1 PNG/BMP of the frame (grid at top-left)
  --obs SOURCE               screenshot that OBS input at native size via obs-websocket
                             (ws://127.0.0.1:4455, --obs-password if auth is on)
  --obs-list                 just list OBS inputs and exit
  --hnode FILE.png           ask a running HNode (FrameSnapshotExporter enabled, UDP 9123)
                             to save its rendered frame to FILE and decode that

Output: a JSON dump in the same shape dev_artnet_dump.py writes
({"universes": {"<index>": [512 ints]}}), consumable by
  dev_snapshot_scene.py --from-dump <json>
which turns it into a Scene in the running QLC+. Pass --scene NAME to do that
directly from here.

Run with the MSYS2 mingw64 python3 (has websockets + Pillow installed).
"""
import argparse
import base64
import hashlib
import io
import json
import os
import socket
import subprocess
import sys
import time
import uuid

BLOCK = 4
# MDMX ("Binary Stage Flight", HNode MdmxSerializer): 6 channels per column of
# 48 blocks, bits MSB-first down the column, 4 CRC blocks under each column, so a
# band is (48 + 4) * 4 = 208 px tall; once the columns run past the texture width
# the next channels wrap into the band below.
MDMX_CHANNELS_PER_COL = 6
MDMX_BLOCKS_PER_COL = MDMX_CHANNELS_PER_COL * 8
MDMX_CRC_BITS = 4
MDMX_BAND = (MDMX_BLOCKS_PER_COL + MDMX_CRC_BITS) * BLOCK
# plain "Binary" (HNode BinarySerializer): 52 blocks per column, LSB-first, no CRC
BINARY_BLOCKS_PER_COL = 52


def decode_grid(img, max_universes=None, x0=0, y0=0, fmt="mdmx", grid_width=None, max_channels=None, bands=1):
    """Return a list of universes (each a list of 512 ints) decoded from a PIL image.

    grid_width: width in px of the grid region to read (columns beyond it belong to
    another region, e.g. the world's VJ grid at x >= 1840); defaults to the image width.
    """
    rgb = img.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    gw = grid_width or (w - x0)
    cols = gw // BLOCK
    if fmt == "mdmx":
        total_channels = cols * MDMX_CHANNELS_PER_COL
        # wrapping bands below the first one (HNode "Turbo Expand" for the light
        # drones); the world's normal LD grid is a single 208 px band, so default 1
        total_channels *= max(1, min(bands, (h - y0) // MDMX_BAND))
    else:
        total_channels = cols * BINARY_BLOCKS_PER_COL // 8
    if max_universes:
        total_channels = min(total_channels, max_universes * 512)
    if max_channels:
        total_channels = min(total_channels, max_channels)
    values = []
    for c in range(total_channels):
        v = 0
        for i in range(8):
            if fmt == "mdmx":
                n = c * 8 + (7 - i)
                x = (n // MDMX_BLOCKS_PER_COL) * BLOCK
                y = (n % MDMX_BLOCKS_PER_COL) * BLOCK
                band = x // gw
                x = x % gw
                y += band * MDMX_BAND
            else:
                n = c * 8 + i
                x = (n // BINARY_BLOCKS_PER_COL) * BLOCK
                y = (n % BINARY_BLOCKS_PER_COL) * BLOCK
            x += x0 + 1
            y += y0 + 1
            if x >= w or y >= h:
                continue
            if px[x, y][0] > 127:
                v |= 1 << i
        values.append(v)
    universes = []
    for u in range(0, len(values), 512):
        frame = values[u:u + 512]
        frame += [0] * (512 - len(frame))
        universes.append(frame)
    return universes


# --- OBS websocket (v5) -----------------------------------------------------

class Obs:
    def __init__(self, uri="ws://127.0.0.1:4455", password=None, timeout=5.0):
        from websockets.sync.client import connect
        self.timeout = timeout
        self.ws = connect(uri, open_timeout=timeout, legacy=True, max_size=64 * 1024 * 1024)
        hello = json.loads(self.ws.recv(timeout=timeout))["d"]
        ident = {"rpcVersion": 1}
        if "authentication" in hello:
            if not password:
                raise SystemExit("OBS websocket requires a password: pass --obs-password (OBS > Tools > WebSocket Server Settings)")
            auth = hello["authentication"]
            secret = base64.b64encode(hashlib.sha256((password + auth["salt"]).encode()).digest()).decode()
            ident["authentication"] = base64.b64encode(hashlib.sha256((secret + auth["challenge"]).encode()).digest()).decode()
        self.ws.send(json.dumps({"op": 1, "d": ident}))
        resp = json.loads(self.ws.recv(timeout=timeout))
        if resp.get("op") != 2:
            raise SystemExit("OBS identify failed: %s" % resp)

    def request(self, request_type, data=None):
        rid = uuid.uuid4().hex[:8]
        self.ws.send(json.dumps({"op": 6, "d": {"requestType": request_type, "requestId": rid, "requestData": data or {}}}))
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            msg = json.loads(self.ws.recv(timeout=self.timeout))
            if msg.get("op") == 7 and msg["d"].get("requestId") == rid:
                st = msg["d"]["requestStatus"]
                if not st.get("result"):
                    raise RuntimeError("%s failed: %s" % (request_type, st))
                return msg["d"].get("responseData", {})
        raise TimeoutError(request_type)

    def close(self):
        self.ws.close()


def obs_screenshot(source, password=None):
    from PIL import Image
    obs = Obs(password=password)
    try:
        # native size: ask for the source's own dimensions, which the screenshot call
        # uses when width/height are omitted
        data = obs.request("GetSourceScreenshot", {"sourceName": source, "imageFormat": "png",
                                                   "imageCompressionQuality": 100})
    finally:
        obs.close()
    b64 = data["imageData"].split(",", 1)[1]
    return Image.open(io.BytesIO(base64.b64decode(b64)))


def obs_list(password=None):
    obs = Obs(password=password)
    try:
        inputs = obs.request("GetInputList").get("inputs", [])
        scene = obs.request("GetCurrentProgramScene").get("currentProgramSceneName")
        items = obs.request("GetSceneItemList", {"sceneName": scene}).get("sceneItems", [])
    finally:
        obs.close()
    print("current scene:", scene)
    for it in items:
        print("  scene item: %-40s kind=%s" % (it.get("sourceName"), it.get("inputKind")))
    print("all inputs:")
    for i in inputs:
        print("  %-40s kind=%s" % (i["inputName"], i["inputKind"]))


# --- HNode FrameSnapshotExporter --------------------------------------------

def hnode_frame(file_path, timeout=5.0):
    from PIL import Image
    resp_port = 9124
    rs = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rs.bind(("127.0.0.1", resp_port))
    rs.settimeout(timeout)
    cmd = json.dumps({"command": "save_frame", "frame_number": 0,
                      "file_path": file_path, "response_port": resp_port})
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(cmd.encode(), ("127.0.0.1", 9123))
    s.close()
    try:
        data, _ = rs.recvfrom(4096)
        print("HNode:", data.decode(errors="replace"))
    except socket.timeout:
        raise SystemExit("no answer from HNode on UDP 9123 - is FrameSnapshotExporter enabled in its Exporters list?")
    finally:
        rs.close()
    return Image.open(file_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png")
    ap.add_argument("--obs", metavar="SOURCE")
    ap.add_argument("--obs-list", action="store_true")
    ap.add_argument("--obs-password")
    ap.add_argument("--hnode", metavar="FILE.png")
    ap.add_argument("--max-universes", type=int, default=None)
    ap.add_argument("--format", choices=("mdmx", "binary"), default="mdmx")
    ap.add_argument("--grid-width", type=int, default=1920, help="px width of the grid (world LD+VJ grid and HNode output: 1920 = 2880 channels)")
    ap.add_argument("--bands", type=int, default=1, help="208 px bands to read (1 = normal; more only in Turbo Expand mode)")
    ap.add_argument("--out", default="grid-dump.json")
    ap.add_argument("--scene", metavar="NAME", help="also create this Scene in QLC+ via dev_snapshot_scene.py")
    ap.add_argument("--scene-path", default="Snapshots")
    args = ap.parse_args()

    if args.obs_list:
        obs_list(args.obs_password)
        return 0

    from PIL import Image
    if args.png:
        img = Image.open(args.png)
    elif args.obs:
        img = obs_screenshot(args.obs, args.obs_password)
    elif args.hnode:
        img = hnode_frame(os.path.abspath(args.hnode))
    else:
        ap.error("one of --png / --obs / --hnode / --obs-list is required")

    print("image %dx%d" % img.size)
    universes = decode_grid(img, args.max_universes, fmt=args.format, grid_width=args.grid_width, bands=args.bands)
    # drop trailing all-zero universes
    while universes and not any(universes[-1]):
        universes.pop()
    dump = {"captured_at": time.strftime("%Y-%m-%dT%H:%M:%S"), "packets": 0,
            "universes": {str(i): u for i, u in enumerate(universes)}}
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(dump, f)
    for i, u in enumerate(universes):
        nz = [(k + 1, v) for k, v in enumerate(u) if v]
        print("universe %d: %d non-zero  %s%s" % (i + 1, len(nz), " ".join("%d=%d" % t for t in nz[:24]),
                                                   " ..." if len(nz) > 24 else ""))
    print("dump written:", args.out)

    if args.scene:
        here = os.path.dirname(os.path.abspath(__file__))
        cmd = [sys.executable, os.path.join(here, "dev_snapshot_scene.py"), "--from-dump", args.out,
               "--name", args.scene, "--path", args.scene_path]
        return subprocess.call(cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
