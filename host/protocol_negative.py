"""Exercise bounded protocol errors and verify the device remains responsive."""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from metalio import Client, Journal, open_serial
from hardware_acceptance import wait_until_ready


def expect_raw_error(link, client, request, expected):
    link.write((json.dumps(request, separators=(",", ":")) + "\n").encode())
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        for packet in client.receive():
            if packet.get("id") == request.get("id"):
                if packet.get("ok") is not False or packet.get("error") != expected:
                    raise RuntimeError(f"expected {expected}, got {packet}")
                return packet
    raise TimeoutError(f"no {expected} response")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()
    journal = Journal(Path(args.log), label="protocol_negative_dev9")
    link = open_serial(args.port)
    try:
        client = Client(link, journal, expected_device="1020ba6e0be0")
        hello = client.hello()
        if hello.get("version") != "1.0.0-dev.9":
            raise RuntimeError(f"unexpected firmware {hello.get('version')}")
        ready = wait_until_ready(client)
        expect_raw_error(link, client,
                         {"protocol": 1, "id": 9001, "cmd": "unknown.command"},
                         "unsupported_command")
        expect_raw_error(link, client,
                         {"protocol": 1, "id": 9002, "cmd": "frame.read", "offset": 48000, "length": 1},
                         "invalid_frame_range")
        expect_raw_error(link, client,
                         {"protocol": 1, "id": 9003, "cmd": "scene.set", "scene": {"version": "bad"}},
                         "invalid_scene")
        link.write(b"not-json\n")
        deadline = time.monotonic() + 3
        invalid_seen = False
        while time.monotonic() < deadline:
            for packet in client.receive():
                if packet.get("error") == "invalid_request":
                    invalid_seen = True
                    break
            if invalid_seen:
                break
        if not invalid_seen:
            raise TimeoutError("no invalid_request response")
        after = client.request("status")
        if not after.get("board_ready") or after.get("boot_id") != client.boot_id:
            raise RuntimeError(f"device changed state after malformed requests: {after}")
        journal.record("negative_regression_pass", {
            "errors": ["unsupported_command", "invalid_frame_range", "invalid_scene", "invalid_request"],
            "boot_id": client.boot_id,
            "status": after,
        })
        print("PASS negative protocol regression", flush=True)
    finally:
        link.close()
        journal.close()


if __name__ == "__main__":
    main()
