#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""An M-VAVE SMC-Mixer to the FM-1 through this computer: every message the mixer sends (over USB or Bluetooth) is passed on to the FM-1's USB MIDI
port, so MIDI IN's Notes: SMC works with the FM-1 plugged into the computer (no USB host, no hub). Needs mido with python-rtmidi (build/venv has both).
    build/venv/bin/python tools/smc_bridge.py            Ctrl-C stops it
"""
import sys
import time

import mido


def pick(names, *words):
    for n in names:
        if any(w.lower() in n.lower() for w in words):
            return n
    return None


def main():
    src = pick(mido.get_input_names(), "SMC-Mixer-Master", "SMC")
    dst = pick(mido.get_output_names(), "Felucca", "FM-1")
    if not src or not dst:
        print("mixer:", src or "not found", "| FM-1:", dst or "not found")
        print("inputs:", mido.get_input_names())
        print("outputs:", mido.get_output_names())
        return 1
    print(f"{src}  ->  {dst}   (Ctrl-C stops)")
    n = 0
    with mido.open_input(src) as i, mido.open_output(dst) as o:
        try:
            while True:
                for m in i.iter_pending():
                    o.send(m)
                    n += 1
                time.sleep(0.002)
        except KeyboardInterrupt:
            pass
    print(f"\n{n} messages passed on")
    return 0


if __name__ == "__main__":
    sys.exit(main())
