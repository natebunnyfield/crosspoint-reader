#!/usr/bin/env python3
"""SPIKE — send CMD:BLEEDIT and stream the resulting log.

Opens the port, waits for the device to finish booting, fires the command that
pushes BleEditorActivity, then prints everything so the SPIKE-HEAP /
SPIKE-LATENCY markers can be read live. Appends to spike-capture.log too.
"""
import glob
import sys
import time

import serial

LOG = "/Users/natebunnyfield/src/crosspoint-reader/.claude/worktrees/ble-keyboard-editor-spike-5211b8/spike-capture.log"
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 600


def find_port():
    p = sorted(glob.glob("/dev/cu.usbmodem*"))
    return p[0] if p else None


deadline = time.time() + DURATION
sent = False
out = open(LOG, "a")
while time.time() < deadline:
    port = find_port()
    if not port:
        time.sleep(1)
        continue
    try:
        with serial.Serial(port, 115200, timeout=1) as ser:
            time.sleep(2)  # let USB CDC settle after enumeration
            while time.time() < deadline:
                if not sent:
                    ser.write(b"CMD:BLEEDIT\n")
                    ser.flush()
                    print(">>> sent CMD:BLEEDIT", flush=True)
                    sent = True
                line = ser.readline()
                if not line:
                    continue
                text = line.decode("utf-8", "replace").rstrip()
                out.write(text + "\n")
                out.flush()
                print(text, flush=True)
    except Exception as exc:
        print(f"[reopen: {exc}]", flush=True)
        sent = False  # device reset; the command must be re-sent
        time.sleep(2)
print("done", flush=True)
