#!/usr/bin/env python3
"""Read back and decode the SD controller log written by m65fdisk-sdlog.prg.

    sdlog_dump.py [-p /dev/ttyUSB0] [-n LAST] [-o raw.bin] [-i raw.bin]

Reads the log from attic RAM ($8000000) over the serial monitor (the
MEGA65 can be running: the log is only read), or from a file saved with
-o earlier, and prints one line per entry.  -n: only the last LAST entries
(default 3000; 0 = all the ring holds).  See SDLOG in fdisk_hal_mega65.c.
"""
import argparse
import re
import struct
import sys
import time

BASE = 0x8000000

COMMANDS = {
    0x00: "reset", 0x01: "end reset / clear", 0x02: "read", 0x03: "write",
    0x04: "multi write first", 0x05: "multi write next", 0x06: "multi write last",
    0x10: "reset (alt)", 0x11: "end reset (alt)", 0x40: "SDHC off", 0x41: "SDHC on",
    0x4D: "gate: MBR", 0x57: "gate: write", 0x61: "QSPI read", 0x81: "map buffer",
    0x82: "unmap buffer", 0xC0: "select internal", 0xC1: "select external",
}


def monitor_reader(port):
    import serial
    s = serial.Serial(port, 2000000, timeout=0.3)

    def read(addr, n):
        out = bytearray()
        while len(out) < n:
            a = addr + len(out)
            for _ in range(5):
                s.reset_input_buffer()
                s.write(("M %x\r" % a).encode())
                time.sleep(0.25)
                text = s.read(20000).decode("latin1")
                rows = dict((int(x, 16), bytes.fromhex(y))
                            for x, y in re.findall(r":0*([0-9A-F]+):([0-9A-F]{32})", text))
                if all(a + i in rows for i in range(0, 256, 16)):
                    break
            else:
                sys.exit("no monitor answer at $%X" % a)
            for i in range(0, 256, 16):
                out += rows[a + i]
        return bytes(out[:n])
    return read


def status(v):
    names = ["sdio busy", "busy", "reset", "mapped", "SDHC", "FSM error", "error", "ext"]
    return ",".join(n for i, n in enumerate(names) if v & (1 << i) and i != 7) or "idle"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default="/dev/ttyUSB0")
    ap.add_argument("-n", "--last", type=int, default=3000)
    ap.add_argument("-o", "--save")
    ap.add_argument("-i", "--load")
    a = ap.parse_args()

    if a.load:
        raw = open(a.load, "rb").read()
        head, body = raw[:16], raw[16:]
    else:
        read = monitor_reader(a.port)
        head = read(BASE, 16)
        body = None
    if head[:4] != b"SDLG":
        sys.exit("no log at $8000000 (header %s)" % head[:4])
    count, size, ring = struct.unpack("<III", head[4:16])
    held = min(count, ring)
    want = held if a.last == 0 else min(held, a.last)
    first = count - want
    if body is None:
        # the wanted entries, in at most two pieces of the ring
        chunks = []
        i = first
        while i < count:
            slot = i % ring
            n = min(count - i, ring - slot)
            chunks.append(read(BASE + 16 + slot * 16, n * 16))
            i += n
        body = b"".join(chunks)
        first_in_body = first
        if a.save:
            open(a.save, "wb").write(head + body)
    else:
        first_in_body = count - len(body) // 16
    print("%d entries written, %d shown (from %d)" % (count, want, first))
    t_prev = None
    for k in range(len(body) // 16):
        e = body[k * 16:(k + 1) * 16]
        idx = first_in_body + k
        typ, val, d686, d687 = e[0], e[1], e[2], e[3]
        polls = struct.unpack("<I", e[12:16])[0]
        if typ in (ord("W"), ord("S")):
            sector = struct.unpack("<I", e[4:8])[0]
            t = struct.unpack("<I", e[8:12])[0]
            dt = "" if t_prev is None else "+%9.3f ms" % (((t - t_prev) & 0xffffffff) / 1000.0)
            t_prev = t
            if typ == ord("W"):
                what = "W $%02X %-18s" % (val, COMMANDS.get(val, ""))
            else:
                what = "S $%02X %-18s" % (val, status(val))
            print("%7d %10.3f ms %s %s sector $%08X D686=$%02X D687=$%02X polls %d"
                  % (idx, t / 1000.0, dt, what, sector, d686, d687, polls))
        elif typ == ord("I"):
            r = e[4:12]
            resp = r[7] << 24 | r[6] << 16 | r[5] << 8 | r[4]
            print("%7d    init record: cd=$%02X step=%d flags=$%02X err=$%02X resp=$%08X"
                  "%s%s%s%s  D687=$%02X"
                  % (idx, r[0], r[1], r[2], r[3], resp,
                     " SPI-FALLBACK" if r[2] & 1 else "", " NO-RESPONSE" if r[2] & 2 else "",
                     " RETRIED" if r[2] & 0x40 else "", " BUSY-TIMEOUT" if r[2] & 0x80 else "", d687))
        elif typ == ord("B"):
            print("%7d    block record %d: %s" % (idx, val, e[4:12].hex(" ")))
        else:
            print("%7d    ? %s" % (idx, e.hex(" ")))


if __name__ == "__main__":
    main()
