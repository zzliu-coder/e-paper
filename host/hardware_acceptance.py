#!/usr/bin/env python3
"""Run the bounded Metalio dev.9 hardware probe; never flashes or resets the device.

The default path does not write Flash, SD, RTC, or network configuration.  It may
sample the microphone and scan nearby Wi-Fi networks, but it does not emit sound.
Pass --storage-write to write/read/remove only the SDK's private SD test file.
Pass --physical only when the device may emit one short tone and one haptic pulse.
Physical sound or vibration still needs the user's observation and is reported
separately from the protocol result.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
from pathlib import Path
import sys
import time

from metalio import Client, Journal, choose_port, open_serial


EXPECTED_DEVICE = "1020ba6e0be0"
READ_ONLY_COMMANDS = (
    "inventory",
    "input.snapshot",
    "power.status",
    "imu.probe",
    "imu.read",
    "audio.info",
    "audio.sample",
    "wifi.status",
    "wifi.scan",
    "sd.status",
    "bt.info",
    "rtc.status",
)
REQUIRED_COMMANDS = {"hello", "ping", "status", *READ_ONLY_COMMANDS}


def _reply_record(reply):
    """Keep protocol success and hardware/physical evidence as separate fields."""
    protocol = "PASS" if reply.get("ok") is True else "FAIL"
    return {
        "protocol": protocol,
        "reported_result": reply.get("result", "PASS" if protocol == "PASS" else "FAIL"),
        "reply": reply,
    }


def _call(name, callback):
    try:
        return name, _reply_record(callback())
    except (ConnectionError, OSError, RuntimeError, TimeoutError, ValueError) as exc:
        return name, {"protocol": "FAIL", "reported_result": "FAIL", "error": str(exc)}


def wait_until_ready(client, timeout=60):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = client.request("status", timeout=3)
        if last.get("board_ready") is True:
            return last
        time.sleep(0.25)
    raise TimeoutError(f"board_ready was not true within {timeout}s; last={last}")


def collect_input_events(client, seconds):
    events = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for packet in client.receive():
            if packet.get("event", "").startswith("input."):
                events.append(packet)
        time.sleep(0.02)
    return events


def run(client, physical=False, input_window=0.0, storage_write=False):
    hello = client.hello()
    commands = set(hello.get("commands", []))
    missing = sorted(REQUIRED_COMMANDS - commands)
    if missing:
        raise RuntimeError("dev.9 probe commands missing: " + ", ".join(missing))

    result = {
        "identity": {
            "state": "PASS",
            "device_id": hello.get("device_id"),
            "boot_id": hello.get("boot_id"),
            "version": hello.get("version"),
            "hardware_probe": hello.get("hardware_probe"),
            "efuse_write": hello.get("efuse_write"),
            "flash_write": hello.get("flash_write"),
        },
        "commands": {},
        "physical_actions": [],
        "input_events": [],
    }
    ready = wait_until_ready(client)
    result["commands"]["status"] = _reply_record(ready)

    for name in READ_ONLY_COMMANDS:
        method = {
            "inventory": client.inventory,
            "input.snapshot": client.input_snapshot,
            "power.status": client.power_status,
            "imu.probe": client.probe_accelerometer,
            "imu.read": client.read_accelerometer,
            "audio.info": client.audio_info,
            "audio.sample": client.audio_sample,
            "wifi.status": client.wifi_status,
            "wifi.scan": client.wifi_scan,
            "sd.status": client.sd_status,
            "bt.info": client.bluetooth_info,
            "rtc.status": client.rtc_status,
        }[name]
        key, record = _call(name, method)
        result["commands"][key] = record

    if storage_write:
        if client.commands is not None and "sd.roundtrip" not in client.commands:
            result["commands"]["sd.roundtrip"] = {
                "protocol": "FAIL",
                "reported_result": "FAIL",
                "error": "firmware does not advertise sd.roundtrip",
            }
        else:
            key, record = _call("sd.roundtrip", client.sd_roundtrip)
            result["commands"][key] = record

    if input_window > 0:
        print(
            f"Input window {input_window:g}s: you may tap the three bottom touch zones "
            "or press the orange/AI (GPIO0) key; no input is also valid.",
            flush=True,
        )
        result["input_events"] = collect_input_events(client, input_window)
        names = sorted({p.get("name", p.get("region", "")) for p in result["input_events"]})
        result["input_observed_names"] = [name for name in names if name]

    if physical:
        print("Physical test: one 35ms haptic pulse and one 150ms 440Hz tone will be requested.", flush=True)
        name, record = _call("haptic.pulse", client.pulse_haptic)
        record["physical_effect"] = "NOT_PROVEN"
        result["physical_actions"].append({"command": name, **record})

        name, record = _call("audio.tone", client.play_tone)
        record["physical_effect"] = "NOT_PROVEN"
        if record.get("protocol") == "PASS":
            try:
                idle = client.wait_tone_idle(timeout=4)
                record["completion"] = "PASS" if idle.get("audio_tone_busy") is False else "NOT_PROVEN"
                record["status_after"] = idle
            except (ConnectionError, OSError, RuntimeError, TimeoutError, ValueError) as exc:
                record["completion"] = "NOT_PROVEN"
                record["completion_error"] = str(exc)
        result["physical_actions"].append({"command": name, **record})

    result["summary"] = {
        "protocol_failures": sum(
            1 for record in result["commands"].values() if record.get("protocol") == "FAIL"
        ),
        "read_only_commands": list(READ_ONLY_COMMANDS),
        "physical_effects": "NOT_PROVEN",
        "input_physical_mapping": "NOT_PROVEN",
        "sd_write_roundtrip": "PASS" if storage_write and
        result["commands"].get("sd.roundtrip", {}).get("reported_result") == "PASS" else
        ("NOT_RUN" if not storage_write else "NOT_PROVEN"),
    }
    return result


def default_path(kind):
    root = Path(__file__).resolve().parents[1]
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    return root / "sessions" / f"{stamp}-hardware-probe.{kind}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--device-id", default=EXPECTED_DEVICE)
    parser.add_argument("--log", help="JSONL evidence log; defaults to sessions/")
    parser.add_argument("--report", help="JSON report; defaults to sessions/")
    parser.add_argument(
        "--input-window",
        type=float,
        default=0.0,
        help="Collect raw input events for this many seconds (0 means no waiting)",
    )
    parser.add_argument(
        "--physical",
        action="store_true",
        help="Explicitly request one haptic pulse and one speaker tone",
    )
    parser.add_argument(
        "--storage-write",
        action="store_true",
        help="Write/read/remove only the SDK private SD roundtrip file",
    )
    args = parser.parse_args()
    if args.input_window < 0 or args.input_window > 120:
        parser.error("--input-window must be between 0 and 120 seconds")

    log_path = Path(args.log) if args.log else default_path("jsonl")
    report_path = Path(args.report) if args.report else default_path("json")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)

    link = None
    journal = Journal(log_path, label="hardware_probe_dev9")
    try:
        port = choose_port(args.port)
        link = open_serial(port)
        journal.record("port_open", {"port": port, "reset_requested": False})
        report = run(Client(link, journal, expected_device=args.device_id),
                     physical=args.physical, input_window=args.input_window,
                     storage_write=args.storage_write)
        report["port"] = port
        report["log"] = str(log_path)
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 1 if report["summary"]["protocol_failures"] else 0
    except (ConnectionError, OSError, RuntimeError, TimeoutError, ValueError) as exc:
        failure = {"state": "FAIL", "error": str(exc), "log": str(log_path)}
        report_path.write_text(json.dumps(failure, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(failure, ensure_ascii=False, indent=2), file=sys.stderr)
        return 1
    finally:
        journal.close()
        if link is not None:
            link.close()


if __name__ == "__main__":
    raise SystemExit(main())
