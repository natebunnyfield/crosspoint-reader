#!/usr/bin/env python3
"""SPIKE — one serial session: save wifi cred, open editor, stream everything.

Single port owner (the parallel-capture lesson cost half a day of phantom
'flaky USB'). Reads the wifi password from SPIKE_WIFI_PASS env, never prints it.
"""
import glob
import os
import sys
import time

import serial

LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "spike-capture.log")
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 900
SSID = os.environ.get("SPIKE_WIFI_SSID", "")
PASS = os.environ.get("SPIKE_WIFI_PASS", "")


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
            time.sleep(2)
            while time.time() < deadline:
                if not sent:
                    if SSID and PASS:
                        ser.write(f"CMD:WIFISAVE:{SSID}:{PASS}\n".encode())
                        ser.flush()
                        print(f">>> sent WIFISAVE for {SSID}", flush=True)
                        time.sleep(1)
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
        sent = False
        time.sleep(2)
print("done", flush=True)
