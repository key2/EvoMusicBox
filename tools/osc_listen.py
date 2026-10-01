#!/usr/bin/env python3
"""Tiny OSC 1.0 listener for testing EvoMusicBox (docs/architecture.md tools/).

Usage: osc_listen.py [port]   (default 8000)

Prints every decoded message with a timestamp:  12.345  /light/flash  [1, 0.5]
Supports i f s b T F h d and #bundle packets.
"""
import socket
import struct
import sys
import time


def _pad4(n):
    return (n + 3) & ~3


def _read_string(data, pos):
    end = data.index(b"\0", pos)
    s = data[pos:end].decode("utf-8", "replace")
    return s, pos + _pad4(end - pos + 1)


def parse_message(data):
    addr, pos = _read_string(data, 0)
    if pos >= len(data) or data[pos:pos + 1] != b",":
        return addr, []
    tags, pos = _read_string(data, pos)
    args = []
    for t in tags[1:]:
        if t == "i":
            (v,) = struct.unpack(">i", data[pos:pos + 4]); pos += 4
        elif t == "f":
            (v,) = struct.unpack(">f", data[pos:pos + 4]); pos += 4
        elif t == "s":
            v, pos = _read_string(data, pos)
        elif t == "b":
            (n,) = struct.unpack(">i", data[pos:pos + 4]); pos += 4
            v = data[pos:pos + n]; pos += _pad4(n)
        elif t == "T":
            v = True
        elif t == "F":
            v = False
        elif t == "h":
            (v,) = struct.unpack(">q", data[pos:pos + 8]); pos += 8
        elif t == "d":
            (v,) = struct.unpack(">d", data[pos:pos + 8]); pos += 8
        else:
            v = "?" + t
        args.append(v)
    return addr, args


def parse_packet(data):
    if data.startswith(b"#bundle"):
        out = []
        pos = 16
        while pos + 4 <= len(data):
            (n,) = struct.unpack(">i", data[pos:pos + 4]); pos += 4
            out.extend(parse_packet(data[pos:pos + n])); pos += n
        return out
    return [parse_message(data)]


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", port))
    print("listening on udp/%d (Ctrl+C to quit)" % port)
    t0 = time.time()
    try:
        while True:
            data, addr = sock.recvfrom(65536)
            for a, args in parse_packet(data):
                print("%8.3f  %-32s %r   from %s:%d" % (time.time() - t0, a, args, addr[0], addr[1]))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
