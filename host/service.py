"""Single-owner USB service. Local Unix socket, sequential commands, no silent retries.

serve holds USB across CLI invocations; call sends a JSON command via that session.
Disconnects invalidate the session. The next call re-handshakes the same device.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import sys
import time

from metalio import Client, Journal, DeviceRebooted, choose_port, open_serial, validate_scene
from hardware_acceptance import wait_until_ready

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOCKET = f"/tmp/metalio-{os.getuid()}.sock"


def receive_line(connection):
    data = bytearray()
    while len(data) <= 8192:
        chunk = connection.recv(1)
        if not chunk:
            raise ConnectionError("Local client closed without a complete request")
        if chunk == b"\n":
            request = json.loads(data)
            if not isinstance(request, dict):
                raise ValueError("Expected a JSON object")
            return request
        data.extend(chunk)
    raise ValueError("Request exceeds 8192 bytes")


class Session:
    def __init__(self, port, journal, atomic_lines=False):
        self.port, self.journal = port, journal
        self.atomic_lines=atomic_lines
        self.link = self.client = None
        self.hello = None
        self.export_dir = ROOT / "sessions/device-selftest"
        self.export_cursor = 0

    def close(self):
        if self.link is not None:
            self.link.close()
        self.link = self.client = None

    def connect(self):
        if self.client is None:
            if self.link is None:
                self.link = open_serial(choose_port(self.port),atomic_lines=True) if self.atomic_lines else open_serial(choose_port(self.port))
            try:
                self.client = Client(self.link, self.journal, expected_device="1020ba6e0be0")
                self.hello = self.client.hello()
                wait_until_ready(self.client)
            except DeviceRebooted:
                self.invalidate_protocol()
                raise
            except Exception:
                self.close()
                raise

    def invalidate_protocol(self):
        # An ESP32-S3 USB-JTAG transport can stay open through esp_restart.
        # Closing/reopening it can reset a pending-verify OTA image a second
        # time, triggering rollback before the application accepts its boot.
        self.client = None
        self.hello = None

    def execute(self, request):
        self.connect()
        command = request.get("cmd")
        args = request.get("args", {})
        if not isinstance(args, dict):
            raise ValueError("args must be an object")
        if set(args) & {"id", "protocol", "cmd", "timeout"}:
            raise ValueError("Reserved protocol arguments")
        if command == "service.status":
            return {"identity": self.hello, "status": self.client.request("status")}
        if command == "display.text":
            return self.client.display_text(args["text"], timeout=20)
        if command == "scene.set":
            return self.client.push_scene(args["scene"])
        return self.client.request(command, **args)

    def collect_logs(self):
        if self.client is None or "selftest.logs" not in (self.client.commands or []):
            return
        files = self.client.request("selftest.logs").get("files", [])
        self.export_dir.mkdir(parents=True, exist_ok=True)
        budget = 4
        if not files:
            return
        self.export_cursor %= len(files)
        ordered = files[self.export_cursor:] + files[:self.export_cursor]
        for name in ordered:
            if not isinstance(name, str) or len(name) != 22 or not name.endswith(".jsonl") or any(c not in "0123456789abcdef" for c in name[:-6]):
                raise ValueError("Unexpected device log filename")
            path = self.export_dir / name
            offset = path.stat().st_size if path.exists() else 0
            while budget:
                reply = self.client.request("selftest.log.read", file=name, offset=offset)
                data = bytes.fromhex(reply["hex"])
                if len(data) != reply["bytes"]:
                    raise ValueError("Log chunk size mismatch")
                if data:
                    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
                    with os.fdopen(fd, "ab") as out:
                        out.write(data)
                    offset += len(data)
                budget -= 1
                if reply["eof"]:
                    self.export_cursor = (files.index(name) + 1) % len(files)
                    break
                self.export_cursor = files.index(name)
            if not budget:
                break


def call(path, request):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(65)
        connection.connect(path)
        connection.sendall((json.dumps(request) + "\n").encode())
        return receive_line(connection)


def serve(args):
    # Refuse existing paths rather than deleting another process's socket.
    if os.path.lexists(args.socket):
        raise RuntimeError(f"Socket already exists: {args.socket}")
    journal = Journal(Path(args.log), label="usb_service")
    session = Session(args.port, journal,args.atomic_lines)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(args.socket)
    os.chmod(args.socket, 0o600)
    server.listen(4)
    server.settimeout(0.2)
    last_ping = time.monotonic()
    try:
        session.connect()
        print(json.dumps({"ready": True, "socket": args.socket, "identity": session.hello}), flush=True)
        running = True
        while running:
            try:
                connection, _ = server.accept()
            except socket.timeout:
                if session.client is not None:
                    try:
                        session.client.receive()
                        if time.monotonic() - last_ping >= 5:
                            session.client.request("ping", timeout=3)
                            try:
                                session.collect_logs()
                            except (ValueError, RuntimeError, OSError) as exc:
                                journal.record("log_export_error", {"error": str(exc)})
                            last_ping = time.monotonic()
                    except DeviceRebooted as exc:
                        journal.record("reboot_rehandshake", {"error": str(exc), "transport_preserved": True})
                        session.invalidate_protocol()
                    except (ConnectionError, OSError, TimeoutError, ValueError, RuntimeError) as exc:
                        journal.record("disconnected", {"error": str(exc)})
                        session.close()
                continue
            with connection:
                connection.settimeout(2)
                try:
                    request = receive_line(connection)
                    if request.get("cmd") == "service.stop":
                        reply = {"ok": True, "stopping": True}
                        running = False
                    else:
                        reply = {"ok": True, "result": session.execute(request)}
                except DeviceRebooted as exc:
                    session.invalidate_protocol()
                    reply = {"ok": False, "error": str(exc), "outcome": "unconfirmed; transport preserved; command not retried"}
                except (ConnectionError, OSError, TimeoutError) as exc:
                    session.close()
                    reply = {"ok": False, "error": str(exc), "outcome": "unconfirmed; command not retried"}
                except (ValueError, RuntimeError, KeyError, TypeError) as exc:
                    reply = {"ok": False, "error": str(exc)}
                try:
                    connection.sendall((json.dumps(reply) + "\n").encode())
                except OSError as exc:
                    journal.record("client_delivery_failed", {"error": str(exc)})
    finally:
        session.close()
        server.close()
        os.unlink(args.socket)
        journal.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", default=DEFAULT_SOCKET)
    sub = parser.add_subparsers(dest="action", required=True)
    server = sub.add_parser("serve")
    server.add_argument("--port")
    lines=server.add_mutually_exclusive_group()
    lines.add_argument("--atomic-lines",dest="atomic_lines",action="store_true",help="macOS: deassert DTR/RTS together; preserve a running application's boot")
    lines.add_argument("--legacy-lines",dest="atomic_lines",action="store_false",help="Explicit legacy serial opening; may reset the target")
    server.set_defaults(atomic_lines=sys.platform=="darwin")
    server.add_argument("--log", default=str(ROOT / "sessions/service.jsonl"))
    client = sub.add_parser("call")
    client.add_argument("command")
    client.add_argument("--args", default="{}")
    watcher = sub.add_parser("watch")
    watcher.add_argument("file", type=Path)
    watcher.add_argument("--once", action="store_true")
    sub.add_parser("check", help="Show a check page and play three audible tones")
    args = parser.parse_args()
    if args.action == "serve":
        serve(args)
    elif args.action == "watch":
        previous = None
        while True:
            try:
                content = args.file.read_bytes()
                if content != previous:
                    scene = validate_scene(json.loads(content))
                    reply = call(args.socket, {"cmd": "scene.set", "args": {"scene": scene}})
                    if not reply.get("ok"):
                        raise RuntimeError(reply)
                    previous = content
                    print(json.dumps(reply, ensure_ascii=False), flush=True)
            except (ValueError, OSError, RuntimeError) as exc:
                if args.once:
                    raise
                print(f"Update pending: {exc}", flush=True)
            if args.once:
                break
            time.sleep(2)
    elif args.action == "check":
        reply = call(args.socket, {"cmd": "display.text", "args": {
            "text": "Metalio SDK\nSCREEN + SOUND CHECK\nThree tones: 440 / 660 / 880 Hz"}})
        if not reply.get("ok"):
            raise RuntimeError(reply)
        print("Playing three tones at volume 80", flush=True)
        for frequency in (440, 660, 880):
            reply = call(args.socket, {"cmd": "audio.tone", "args": {
                "duration_ms": 500, "frequency_hz": frequency, "volume": 80}})
            if not reply.get("ok"):
                raise RuntimeError(reply)
            deadline = time.monotonic() + 5
            while True:
                state = call(args.socket, {"cmd": "status"})
                if not state.get("ok"):
                    raise RuntimeError(state)
                result = state["result"]
                if not result["audio_tone_busy"]:
                    if result["audio_tone_last_result"] != 0 or result["audio_tone_samples"] != 8000:
                        raise RuntimeError(result)
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError("Tone outcome unconfirmed")
                time.sleep(0.1)
            time.sleep(0.5)
        print("Device completed display and audio writes. Confirm sight/sound separately.")
    else:
        reply = call(args.socket, {"cmd": args.command, "args": json.loads(args.args)})
        print(json.dumps(reply, ensure_ascii=False, indent=2))
        if not reply.get("ok"):
            raise SystemExit(1)


if __name__ == "__main__":
    main()
