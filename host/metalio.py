#!/usr/bin/env python3
"""Local Metalio fixture client. No flash, erase, reset or eFuse command exists here."""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import sys
import time

PRODUCT = "metalio-personal-sdk"
WIDTH, HEIGHT = 800, 480
MAX_LINE = 16384
ALLOWED = {"hello", "ping", "status", "inventory", "input.snapshot", "power.status", "wifi.status",
           "wifi.scan", "sd.status", "sd.roundtrip", "bt.info", "rtc.status", "rtc.set", "audio.info", "audio.sample", "audio.tone", "scene.set",
           "scene.get", "frame.read", "i2c.recover", "imu.probe", "imu.enable", "imu.read",
           "haptic.pulse", "display.text", "job.get", "selftest.status", "selftest.open",
           "selftest.run", "selftest.logs", "selftest.log.read",
           "storage.usb",
           "inkdesk.status", "inkdesk.open", "inkdesk.tap", "inkdesk.key",
           "inkdesk.ui.open", "inkdesk.ui.frame", "inkdesk.fontlab.log",
           "inkdesk.fontbench.frame", "inkdesk.fontbench.open", "inkdesk.fontbench.config",
           "inkdesk.fontbench.control", "inkdesk.fontbench.state", "inkdesk.fontbench.lock", "inkdesk.fontbench.log",
           "inkdesk.fontlab4.page", "inkdesk.fontlab4.log",
           "paper.status", "paper.open", "paper.tap", "paper.command", "paper.transfer", "paper.maintenance", "paper.file", "paper.network", "paper.bluetooth",
           "book.begin", "book.chunk", "book.commit", "book.abort", "book.read"}


def validate_scene(scene):
    if not isinstance(scene, dict) or set(scene) != {"version", "rects"}:
        raise ValueError("Scene requires exactly version and rects")
    version = scene["version"]
    if not isinstance(version, str) or not 1 <= len(version) <= 48 or not version.isascii() or not version.isprintable():
        raise ValueError("version must be 1..48 printable ASCII characters")
    if not isinstance(scene["rects"], list) or len(scene["rects"]) > 24:
        raise ValueError("At most 24 rectangles")
    for r in scene["rects"]:
        if not isinstance(r, dict) or set(r) != {"x", "y", "w", "h", "black"}:
            raise ValueError("Each rectangle requires x,y,w,h,black")
        if any(type(r[k]) is not int for k in ("x", "y", "w", "h")) or type(r["black"]) is not bool:
            raise ValueError("Coordinates are integers; black is a boolean")
        if not (0 <= r["x"] < WIDTH and 0 <= r["y"] < HEIGHT and 0 < r["w"] <= WIDTH - r["x"] and
                0 < r["h"] <= HEIGHT - r["y"]):
            raise ValueError("Rectangle outside the 800x480 native framebuffer")
    if len(json.dumps(scene, separators=(",", ":")).encode()) > 3600:
        raise ValueError("Scene exceeds transport budget")
    return scene


def render_scene(scene):
    validate_scene(scene)
    frame = bytearray(b"\xff" * (WIDTH * HEIGHT // 8))
    for r in scene["rects"]:
        for y in range(r["y"], r["y"] + r["h"]):
            for x in range(r["x"], r["x"] + r["w"]):
                at, mask = y * 100 + x // 8, 0x80 >> (x % 8)
                if r["black"]:
                    frame[at] &= ~mask
                else:
                    frame[at] |= mask
    return bytes(frame)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Decoder:
    """Bounded line parser; preserves unrelated OEM/ROM logs as raw evidence."""
    def __init__(self):
        self.pending = bytearray()
        self.discarding = False

    def feed(self, data):
        out = []
        for b in data:
            if b == 10:
                if self.discarding:
                    out.append(("framing_error", {"error": "oversize_line"}))
                else:
                    line = self.pending.decode("utf-8", errors="replace").rstrip("\r")
                    if line.startswith("ML1 "):
                        try:
                            packet = json.loads(line[4:])
                            if not isinstance(packet, dict) or packet.get("protocol") != 1:
                                raise ValueError("wrong envelope")
                            out.append(("packet", packet))
                        except (ValueError, TypeError):
                            out.append(("framing_error", {"raw": line}))
                    elif line:
                        out.append(("raw", {"text": line}))
                self.pending.clear()
                self.discarding = False
            elif not self.discarding:
                if len(self.pending) >= MAX_LINE:
                    self.discarding = True
                    self.pending.clear()
                else:
                    self.pending.append(b)
        return out


class Journal:
    def __init__(self, path=None, label=None):
        self.label = label
        self.file = None
        if path:
            path = Path(path)
            path.parent.mkdir(parents=True, exist_ok=True)
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
            self.file = os.fdopen(fd, "a", encoding="utf-8")

    def record(self, kind, payload):
        row = {"host_time": dt.datetime.now(dt.timezone.utc).isoformat(), "kind": kind, "payload": payload}
        if self.label:
            row["label"] = self.label
        if self.file:
            self.file.write(json.dumps(row, ensure_ascii=False) + "\n")
            self.file.flush()
        return row

    def close(self):
        if self.file:
            self.file.close()


def ports():
    from serial.tools import list_ports
    return [{"port": p.device, "vid": p.vid, "pid": p.pid, "serial": p.serial_number,
             "description": p.description} for p in list_ports.comports()]


def choose_port(explicit=None):
    if explicit:
        return explicit
    candidates = [p for p in ports() if p["vid"] == 0x303A and p["pid"] == 0x1001]
    if len(candidates) != 1:
        raise ConnectionError(f"Expected one Espressif USB device, found {len(candidates)}; use --port if needed")
    return candidates[0]["port"]


def open_serial(port, atomic_lines=False):
    import serial
    if atomic_lines:
        from serial_quiet import open_quiet_serial
        return open_quiet_serial(port)
    # Set line states before opening. Never toggle reset/download control lines.
    link = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2, exclusive=True)
    link.dtr = False
    link.rts = False
    link.port = port
    link.open()
    return link


class DeviceRebooted(ConnectionError):
    """Protocol identity changed on a live transport; do not toggle USB lines."""


class Client:
    def __init__(self, transport, journal, expected_device=None):
        self.transport, self.journal = transport, journal
        self.decoder = Decoder()
        self.next_id = 1
        self.expected_device = expected_device
        self.device_id = self.boot_id = None
        self.last_seq = None
        self.last_packet = time.monotonic()
        self.commands = None

    def receive(self):
        found = []
        for kind, p in self.decoder.feed(self.transport.read(4096)):
            self.journal.record(kind, p)
            if kind != "packet":
                continue
            self.last_packet = time.monotonic()
            if self.boot_id and p.get("boot_id") != self.boot_id:
                raise DeviceRebooted("Device rebooted; a fresh identity handshake is required")
            seq = p.get("seq")
            if type(seq) is int:
                if self.last_seq is not None and seq != self.last_seq + 1:
                    self.journal.record("sequence_gap", {"previous": self.last_seq, "current": seq})
                self.last_seq = seq
            found.append(p)
        return found

    def sync_startup(self, timeout=0.8):
        """Opening macOS USB CDC can reset ESP32-S3. Drain ROM/app boot output first."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            packets = self.receive()
            for p in packets:
                if p.get("event") == "boot" and p.get("boot_id"):
                    self.boot_id = p["boot_id"]
                    self.device_id = p.get("device_id")
            if packets:
                # Heartbeats/boot packets prove the app is alive; leave a short
                # settling window for USB CDC after a reset before the request.
                deadline = min(deadline, time.monotonic() + 0.15)
            time.sleep(0.02)

    def request(self, cmd, timeout=30, **args):
        if cmd not in ALLOWED:
            raise ValueError("Command is outside the fixture allowlist")
        if cmd != "hello" and self.commands is not None and cmd not in self.commands:
            raise ValueError("Connected firmware does not support this command")
        if cmd != "hello" and self.device_id is None:
            raise ConnectionError("Identity handshake required")
        rid = self.next_id
        self.next_id += 1
        req = {"protocol": 1, "id": rid, "cmd": cmd, **args}
        data = (json.dumps(req, separators=(",", ":")) + "\n").encode()
        if len(data) > 4097:
            raise ValueError("Request exceeds firmware line budget")
        self.journal.record("request", req)
        sent = self.transport.write(data)
        if sent != len(data):
            raise ConnectionError("Partial serial write")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for p in self.receive():
                if p.get("id") == rid:
                    if p.get("ok") is not True:
                        raise RuntimeError(p.get("error", "device_error"))
                    return p
        raise TimeoutError(f"No reply to {cmd}; request outcome is unconfirmed (no blind retry)")

    def hello(self):
        self.sync_startup()
        p = self.request("hello", timeout=3)
        if (p.get("product") != PRODUCT or p.get("board") != "metalio_eink4" or
                not p.get("device_id") or not p.get("boot_id")):
            raise ConnectionError("This is not the Metalio diagnostic firmware; original firmware may still be running")
        if self.expected_device and p["device_id"] != self.expected_device:
            raise ConnectionError("Different physical device; refusing automatic reconnect")
        self.device_id, self.boot_id = p["device_id"], p["boot_id"]
        self.commands = set(p["commands"]) if "commands" in p else None
        self.journal.record("verified_identity", p)
        return p

    def display_text(self, text, timeout=10):
        if not isinstance(text, str) or len(text.encode("utf-8")) > 256 or "\\u0000" in text:
            raise ValueError("Text must be at most 256 UTF-8 bytes")
        receipt = self.request("display.text", text=text, timeout=2)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = self.request("job.get", timeout=2)
            if result.get("job_done") == receipt["job_id"]:
                if result.get("job_result") != 0:
                    raise RuntimeError("Display job failed: " + str(result["job_result"]))
                return result
            time.sleep(0.1)
        raise TimeoutError("Display outcome unconfirmed")

    def push_scene(self, scene):
        expected = sha(render_scene(scene))
        receipt = self.request("scene.set", timeout=45, scene=scene)
        readback = self.request("scene.get")
        if (receipt.get("frame_sha256") != expected or readback.get("frame_sha256") != expected or
                readback.get("scene_version") != scene["version"] or
                readback.get("scene_revision") != receipt.get("scene_revision")):
            raise RuntimeError("Scene RAM hash/version readback mismatch")
        self.journal.record("scene_verified", {"version": scene["version"], "frame_sha256": expected,
                                                "ram_result": "PASS", "visual_result": "NOT_PROVEN"})
        return readback

    def read_frame(self):
        initial = self.request("scene.get")
        if (not initial.get("frame_sha256") or initial.get("scene_revision", 0) <= 0 or
                initial.get("scene_version") in {None, "boot"}):
            raise RuntimeError("No scene has been displayed")
        frame = bytearray()
        for offset in range(0, 48000, 512):
            count = min(512, 48000 - offset)
            part = self.request("frame.read", offset=offset, length=count)
            if part.get("offset") != offset or part.get("scene_revision") != initial.get("scene_revision"):
                raise RuntimeError("Frame changed during readback")
            block = bytes.fromhex(part["hex"])
            if len(block) != count:
                raise RuntimeError("Truncated frame block")
            frame.extend(block)
        if sha(frame) != initial["frame_sha256"]:
            raise RuntimeError("Full framebuffer SHA-256 mismatch")
        self.journal.record("frame_verified", {"sha256": sha(frame), "bytes": len(frame), "scope": "RAM framebuffer"})
        return bytes(frame)

    def probe_accelerometer(self):
        """Read SC7A20H WHO_AM_I only; no sensor configuration write."""
        return self.request("imu.probe")

    def enable_accelerometer(self):
        """Apply the official fixed ±2g/100Hz setup, explicitly requested by the caller."""
        return self.request("imu.enable")

    def read_accelerometer(self):
        return self.request("imu.read")

    def pulse_haptic(self, duration_ms=35):
        return self.request("haptic.pulse", duration_ms=duration_ms)

    def inventory(self):
        return self.request("inventory")

    def input_snapshot(self):
        return self.request("input.snapshot")

    def power_status(self):
        return self.request("power.status")

    def audio_info(self):
        return self.request("audio.info")

    def audio_sample(self, timeout=15):
        """Capture one bounded microphone window; no audio is played."""
        return self.request("audio.sample", timeout=timeout)

    def play_tone(self, duration_ms=150, frequency_hz=440, timeout=3):
        """Request a bounded tone; physical sound remains a separate acceptance claim."""
        return self.request("audio.tone", duration_ms=duration_ms, frequency_hz=frequency_hz,
                            timeout=timeout)

    def wifi_status(self):
        return self.request("wifi.status")

    def wifi_scan(self, timeout=15):
        """Run the device's local bounded Wi-Fi scan (no cloud POST)."""
        return self.request("wifi.scan", timeout=timeout)

    def sd_status(self):
        return self.request("sd.status")

    def sd_roundtrip(self, timeout=5):
        """Write/read/remove only the SDK's private SD diagnostic file."""
        return self.request("sd.roundtrip", timeout=timeout)

    def bluetooth_info(self):
        return self.request("bt.info")

    def rtc_status(self):
        return self.request("rtc.status")

    def wait_tone_idle(self, timeout=15, interval=0.05):
        """Wait for the asynchronous tone task without retrying the tone request."""
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            last = self.request("status", timeout=min(2, max(0.2, deadline - time.monotonic())))
            if not last.get("audio_tone_busy", False):
                return last
            time.sleep(interval)
        raise TimeoutError("Audio tone completion unconfirmed")


def save_pbm(path, frame):
    # PBM uses 1=black; the SDK's buffer uses 0=black.
    Path(path).write_bytes(b"P4\n800 480\n" + bytes(b ^ 255 for b in frame))


def summarize(path):
    """Evidence summary, not automatic pin assignment or physical acceptance."""
    identities, touches, masks, addresses, reboots = {}, set(), set(), {}, set()
    raw_lines = gaps = scenes = frame_reads = 0
    imu_probes = imu_samples = haptic_pulses = i2c_recoveries = 0
    hardware_replies = {name: 0 for name in (
        "inventory", "input_snapshot", "power_status", "audio_info", "audio_sample", "wifi_status",
        "wifi_scan", "sd_status", "sd_roundtrip", "bluetooth_info", "rtc_status")}
    wifi = None
    labels = set()
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        try:
            row = json.loads(line)
            p, kind = row["payload"], row["kind"]
        except (ValueError, KeyError, TypeError):
            continue
        if row.get("label"):
            labels.add(row["label"])
        if kind == "verified_identity":
            identities[p["device_id"]] = p.get("version")
            reboots.add(p.get("boot_id"))
        if kind == "sequence_gap" or kind == "framing_error":
            gaps += 1
        if kind == "raw":
            raw_lines += 1
        if kind == "scene_verified":
            scenes += 1
        if kind == "frame_verified":
            frame_reads += 1
        if kind == "packet" and p.get("id") is not None:
            if p.get("address") == 0x19 and "who_am_i" in p:
                imu_probes += 1
            if p.get("ax_mg") is not None and p.get("ay_mg") is not None and p.get("az_mg") is not None:
                imu_samples += 1
            if p.get("pulse_count") is not None:
                haptic_pulses += 1
            if p.get("recovered") is not None:
                i2c_recoveries += 1
            if "inputs" in p and "tca9555" in p:
                hardware_replies["input_snapshot"] += 1
            if "screen" in p and "io_expander" in p and "touch" in p:
                hardware_replies["inventory"] += 1
            if isinstance(p.get("battery"), dict) and ("voltage_mv" in p["battery"] or "gauge_ready" in p["battery"]):
                hardware_replies["power_status"] += 1
            if "codec_created" in p or "input_sample_rate" in p:
                hardware_replies["audio_info"] += 1
            if "sample_result" in p and "samples_requested" in p:
                hardware_replies["audio_sample"] += 1
            if "wifi" in p and isinstance(p["wifi"], dict):
                hardware_replies["wifi_status"] += 1
            if "aps" in p and "scan_result" in p:
                hardware_replies["wifi_scan"] += 1
                wifi = "scan_completed" if p.get("scan_ok") else "scan_failed"
            if "mount_point" in p and "card_handle" in p:
                hardware_replies["sd_status"] += 1
            if "write_roundtrip" in p and "test_path" in p:
                hardware_replies["sd_roundtrip"] += 1
            if "uart_initialized" in p and "tx_gpio" in p:
                hardware_replies["bluetooth_info"] += 1
            if "time_valid" in p or ("ready" in p and "year" in p):
                hardware_replies["rtc_status"] += 1
        if kind == "packet":
            if p.get("event") == "input.touch" and p.get("region") not in {"none", "unknown", None}:
                touches.add(p["region"])
            if p.get("event") == "input.keys" and p.get("sample") == "change":
                mask = p.get("changed_mask", 0) & ~p.get("configured_outputs_mask", 0)
                if mask:
                    masks.add(mask)
            for a in p.get("addresses", []):
                addresses.setdefault(hex(a["address"]), set()).add(bool(a["ack"]))
    return {"identity_handshake": "PASS" if identities else "NOT_PROVEN", "devices": identities,
            "observed_boot_sessions": len(reboots), "touch_regions_observed": sorted(touches),
            "expander_input_change_masks": [hex(m) for m in sorted(masks)],
            "i2c_ack_observations": {k: sorted(v) for k, v in addresses.items()},
            "wifi_observation": wifi, "scene_ram_checks": scenes, "full_frame_readbacks": frame_reads,
            "log_gaps_or_framing_errors": gaps, "raw_log_lines": raw_lines, "user_session_labels": sorted(labels),
            "imu_probe_replies": imu_probes, "imu_sample_replies": imu_samples,
            "haptic_pulse_replies": haptic_pulses, "i2c_recovery_replies": i2c_recoveries,
            "hardware_probe_replies": hardware_replies,
            "visual_display_acceptance": "NOT_PROVEN", "audio_mic_bt_sd": "NOT_PROVEN",
            "caution": "Address ACK and correlated input changes are clues, not chip identity or verified wiring."}


def run_connected(args, journal):
    duration = args.seconds if args.seconds is not None else (60 if args.action == "monitor" else 0)
    end = time.monotonic() + duration if duration > 0 else float("inf")
    expected_device = args.device_id
    scene_digest = None
    reconnects = 0
    while time.monotonic() < end:
        transport = None
        try:
            port = choose_port(args.port)
            transport = open_serial(port)
            journal.record("port_open", {"port": port, "reset_requested": False})
            if args.action == "passive":
                decoder = Decoder()
                while time.monotonic() < end:
                    for kind, p in decoder.feed(transport.read(4096)):
                        journal.record(kind, p)
                        print(json.dumps(p, ensure_ascii=False), flush=True)
                return
            client = Client(transport, journal, expected_device)
            hello = client.hello()
            expected_device = client.device_id
            scene_digest = None  # RAM content must be restored after every reconnect/boot.
            print(f"Connected {expected_device}, SDK {hello['version']}, boot {client.boot_id}", flush=True)
            if args.action == "run":
                for cmd in ("ping", "status", "inventory", "input.snapshot", "power.status",
                            "imu.probe", "imu.read", "audio.info", "audio.sample", "wifi.status", "wifi.scan", "sd.status",
                            "bt.info", "rtc.status"):
                    if client.commands is not None and cmd not in client.commands:
                        continue
                    print(json.dumps(client.request(cmd), ensure_ascii=False))
                if args.scene:
                    print(json.dumps(client.push_scene(json.loads(Path(args.scene).read_text())), ensure_ascii=False))
                return
            if args.action == "command":
                # client.hello() already performed the identity handshake.
                # Sending a second hello is unnecessary and used to expose a
                # firmware-side startup race, so report that verified reply
                # directly for `command hello`.
                result = hello if args.cmd == "hello" else client.request(args.cmd)
                print(json.dumps(result, ensure_ascii=False))
                return
            if args.action == "readback":
                if args.scene:
                    scene = validate_scene(json.loads(Path(args.scene).read_text()))
                    client.push_scene(scene)
                save_pbm(args.output, client.read_frame())
                print(f"Verified RAM framebuffer saved to {args.output}; physical image still requires observation")
                return
            last_ping = 0
            last_scene_attempt = 0
            last_file_error = None
            while time.monotonic() < end:
                for p in client.receive():
                    if p.get("event", "").startswith("input."):
                        print(json.dumps(p, ensure_ascii=False), flush=True)
                if time.monotonic() - last_ping >= 5:
                    client.request("ping", timeout=5)
                    last_ping = time.monotonic()
                if args.action == "watch":
                    try:
                        content = Path(args.scene).read_bytes()
                        digest = sha(content)
                        if digest != scene_digest and time.monotonic() - last_scene_attempt >= 2.5:
                            scene = validate_scene(json.loads(content))
                            last_scene_attempt = time.monotonic()
                            try:
                                result = client.push_scene(scene)
                            except RuntimeError as exc:
                                if str(exc) == "refresh_rate_limited":
                                    continue
                                raise
                            scene_digest = digest
                            last_file_error = None
                            print(f"RAM verified: {scene['version']} / {result['frame_sha256'][:12]}", flush=True)
                    except (ValueError, OSError) as exc:
                        if str(exc) != last_file_error:
                            journal.record("scene_file_error", {"error": str(exc)})
                            print(f"Scene file: {exc}", file=sys.stderr)
                            last_file_error = str(exc)
                    time.sleep(0.25)
        except (ConnectionError, TimeoutError, OSError, RuntimeError) as exc:
            journal.record("connection_error", {"error": str(exc)})
            print(str(exc), file=sys.stderr, flush=True)
            if args.action not in {"monitor", "watch", "passive"}:
                raise
            reconnects += 1
            time.sleep(min(5, max(1, reconnects)))
        finally:
            if transport:
                transport.close()
                journal.record("port_close", {})


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Exact serial port; default: one Espressif USB device")
    parser.add_argument("--device-id", help="Restrict connection to a previously observed fixture device ID")
    parser.add_argument("--log", help="Private local JSONL evidence log")
    parser.add_argument("--label", help="Physical action being observed, e.g. press_ai_key; only a user annotation")
    parser.add_argument("--seconds", type=float, help="Monitor duration; 0 means until Ctrl-C")
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("ports")
    sub.add_parser("passive", help="Receive original firmware logs; send no data or reset requests")
    sub.add_parser("monitor", help="Identity-gated fixture event monitor with reconnect")
    run = sub.add_parser("run", help="One pass: ping, telemetry, known-address inventory")
    run.add_argument("--scene")
    watch = sub.add_parser("watch", help="Live reload a RAM scene file and verify its framebuffer hash")
    watch.add_argument("scene")
    cmd = sub.add_parser("command")
    cmd.add_argument("cmd", choices=["hello", "ping", "status", "inventory", "input.snapshot", "power.status",
                                      "audio.info", "audio.sample", "audio.tone", "wifi.status", "wifi.scan", "sd.status",
                                      "sd.roundtrip",
                                      "bt.info", "rtc.status", "scene.get", "i2c.recover", "imu.probe",
                                      "imu.enable", "imu.read", "haptic.pulse"])
    readback = sub.add_parser("readback")
    readback.add_argument("output")
    readback.add_argument("--scene", help="Set this scene in the same USB session before reading the frame")
    report = sub.add_parser("report")
    report.add_argument("file")
    preview = sub.add_parser("preview")
    preview.add_argument("scene")
    preview.add_argument("output")
    args = parser.parse_args()
    if args.action == "ports":
        print(json.dumps(ports(), ensure_ascii=False, indent=2)); return
    if args.action == "report":
        print(json.dumps(summarize(args.file), ensure_ascii=False, indent=2)); return
    if args.action == "preview":
        frame = render_scene(json.loads(Path(args.scene).read_text()))
        save_pbm(args.output, frame)
        print(f"Offline preview SHA-256 {sha(frame)}"); return
    if not args.log:
        stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        args.log = str(Path(__file__).resolve().parents[1] / "sessions" / f"{stamp}-{args.action}.jsonl")
    print(f"Evidence: {args.log}", flush=True)
    journal = Journal(args.log, args.label)
    try:
        run_connected(args, journal)
    except KeyboardInterrupt:
        print("Stopped; device firmware continues running")
    except (ConnectionError, TimeoutError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        journal.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
