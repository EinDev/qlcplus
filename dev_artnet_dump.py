#!/usr/bin/env python3
"""Capture the current ArtNet (ArtDmx) state on this machine and dump it as JSON.

Listens on UDP 6454 for a few seconds, keeps the latest 512 values per
port-address (net/subnet/universe -> flat 0-based universe index), and writes
{"universes": {"<index>": [512 ints]}, "sources": {...}} to the output file.
Also prints, per universe, the non-zero channels (1-based) for a quick look.

Windows note: several apps may already be bound to 6454 (QLC+, HNode,
Artnetominator). We bind with SO_REUSEADDR; while this runs, unicast packets
to 127.0.0.1:6454 may be delivered to this socket instead of one of them, so
keep the capture short.

Usage:
  python dev_artnet_dump.py [--seconds 3] [--out artnet-dump.json] [--bind 0.0.0.0]
"""
import argparse
import json
import socket
import struct
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--out", default="artnet-dump.json")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, 6454))
    sock.settimeout(0.2)

    universes = {}   # index -> bytearray(512)
    frames_by_source = {}  # "index|ip:port" -> bytearray(512)
    sources = {}     # index -> {"from": "ip:port", "packets": n}
    packets = 0
    deadline = time.time() + args.seconds
    while time.time() < deadline:
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            continue
        if len(data) < 18 or data[:8] != b"Art-Net\0":
            continue
        opcode = struct.unpack("<H", data[8:10])[0]
        if opcode != 0x5000:  # ArtDmx
            continue
        sub_uni = data[14]
        net = data[15]
        length = struct.unpack(">H", data[16:18])[0]
        index = (net << 8) | sub_uni
        dmx = data[18:18 + min(length, 512)]
        buf = universes.setdefault(index, bytearray(512))
        buf[:len(dmx)] = dmx
        src = sources.setdefault(index, {"from": "%s:%d" % addr, "packets": 0})
        src["packets"] += 1
        # keep the latest frame per (universe, sender) too, so two senders on
        # the same universe (e.g. QLC+ and a grid-node decoder) stay separable
        key = "%d|%s:%d" % (index, addr[0], addr[1])
        fb = frames_by_source.setdefault(key, bytearray(512))
        fb[:len(dmx)] = dmx
        packets += 1
    sock.close()

    out = {
        "captured_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "seconds": args.seconds,
        "packets": packets,
        "sources": {str(k): v for k, v in sorted(sources.items())},
        "universes": {str(k): list(v) for k, v in sorted(universes.items())},
        "frames_by_source": {k: list(v) for k, v in sorted(frames_by_source.items())},
    }
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(out, f)

    print("packets: %d, universes: %s -> %s" % (packets, sorted(universes), args.out))
    print("senders: " + ", ".join(sorted(set(k.split("|")[1] for k in frames_by_source))))
    if not args.quiet:
        for idx in sorted(universes):
            buf = universes[idx]
            nz = [(i + 1, v) for i, v in enumerate(buf) if v]
            print("universe %d (ArtNet %d, from %s): %d non-zero" % (idx + 1, idx, sources[idx]["from"], len(nz)))
            print("   " + " ".join("%d=%d" % c for c in nz[:120]) + (" ..." if len(nz) > 120 else ""))
    return 0 if packets else 1


if __name__ == "__main__":
    sys.exit(main())
