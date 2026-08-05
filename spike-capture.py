#!/usr/bin/env python3
"""SPIKE (branch spike/ble-editor) — wait for the X4, flash it, capture the log.

scripts/debugging_monitor.py opens a matplotlib window, which is the right tool
for watching live but useless for an unattended capture. This does the headless
half: block until the USB CDC port appears, upload, then log every line to a
file so the SPIKE-HEAP / SPIKE-LATENCY markers can be read back afterwards.

  ./spike-capture.py [--wait SECONDS] [--capture SECONDS] [--no-upload]
"""
import argparse
import glob
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "spike-capture.log")


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def wait_for_port(timeout):
    deadline = time.time() + timeout
    seen = None
    while time.time() < deadline:
        seen = find_port()
        if seen:
            return seen
        time.sleep(1)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wait", type=int, default=900, help="seconds to wait for the device")
    ap.add_argument("--capture", type=int, default=240, help="seconds of serial to capture")
    ap.add_argument("--no-upload", action="store_true")
    # Re-arming a capture must not truncate what the previous run already saw.
    ap.add_argument("--append", action="store_true")
    args = ap.parse_args()

    print("waiting for /dev/cu.usbmodem* ...", flush=True)
    port = wait_for_port(args.wait)
    if not port:
        print("TIMEOUT: no X4 on USB", file=sys.stderr)
        return 1
    print(f"found {port}", flush=True)

    if not args.no_upload:
        # The device re-enumerates after the reset esptool issues, so re-find it.
        rc = subprocess.call(["bash", os.path.join(HERE, "spike-build.sh"), "upload"], cwd=HERE)
        if rc != 0:
            print(f"upload failed rc={rc}", file=sys.stderr)
            return rc
        time.sleep(2)
        port = wait_for_port(60) or port
        print(f"post-upload port {port}", flush=True)

    import serial  # pyserial ships with the PlatformIO venv

    print(f"capturing {args.capture}s to {LOG}", flush=True)
    deadline = time.time() + args.capture
    with open(LOG, "a" if args.append else "w") as out:
        while time.time() < deadline:
            try:
                with serial.Serial(port, 115200, timeout=1) as ser:
                    while time.time() < deadline:
                        line = ser.readline()
                        if not line:
                            continue
                        text = line.decode("utf-8", "replace").rstrip()
                        out.write(text + "\n")
                        out.flush()
                        if "SPIKE-" in text or "BLESPIKE" in text:
                            print(text, flush=True)
            except Exception as exc:  # port drops on reset/sleep — reopen
                print(f"[reopen: {exc}]", flush=True)
                time.sleep(2)
                port = wait_for_port(30) or port
    print("capture done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
