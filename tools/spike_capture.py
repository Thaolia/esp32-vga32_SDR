#!/usr/bin/env python3
"""Spike phase 0 : valide les captures CAP16 du firmware esp-sdr stock."""
import argparse
import sys
import zlib

import serial


def command(port, line):
    port.write((line + "\n").encode())
    return port.readline().decode(errors="replace").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--count", type=int, default=50)
    a = ap.parse_args()
    ok = bad_header = bad_crc = 0
    us, rms = 0, 0.0
    with serial.Serial(a.port, 2000000, timeout=2) as p:
        p.reset_input_buffer()
        print(command(p, "INFO"))
        print(command(p, "FREQ 2437"))
        for _ in range(a.count):
            p.write(b"CAP16 1024 1\n")  # rate 1 → horloge 40 MS/s
            hdr = p.readline().decode(errors="replace").split()
            if len(hdr) != 4 or hdr[0] != "DATA":
                bad_header += 1
                print("header:", hdr)
                continue
            n, crc, us = int(hdr[1]), int(hdr[2], 16), int(hdr[3])
            data = p.read(n * 2)
            if len(data) != n * 2 or (zlib.crc32(data) & 0xFFFFFFFF) != crc:
                bad_crc += 1
                continue
            iq = [b - 256 if b > 127 else b for b in data]
            rms = (sum(v * v for v in iq) / len(iq)) ** 0.5
            ok += 1
    print(f"ok={ok}/{a.count} bad_header={bad_header} bad_crc={bad_crc} last_us={us} rms={rms:.1f}")
    return 0 if ok == a.count else 1


if __name__ == "__main__":
    sys.exit(main())
