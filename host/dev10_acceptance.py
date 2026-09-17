"""Exercise dev.10 through the persistent service; retain every result."""
import argparse
import json
from pathlib import Path
import time

from service import call, DEFAULT_SOCKET
from metalio import Journal, render_scene, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", default=DEFAULT_SOCKET)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tones", action="store_true")
    parser.add_argument("--updates", type=int, default=100)
    args = parser.parse_args()
    journal = Journal(args.output, label="dev10_acceptance")
    initial_boot = None

    def request(cmd, **parameters):
        reply = call(args.socket, {"cmd": cmd, "args": parameters})
        journal.record(cmd, reply)
        if not reply.get("ok"):
            raise RuntimeError(reply)
        result = reply["result"]
        if isinstance(result, dict) and result.get("boot_id") and initial_boot:
            if result["boot_id"] != initial_boot:
                raise RuntimeError("Unexpected reboot")
        return result

    try:
        identity = request("service.status")
        assert identity["identity"]["version"] == "1.0.0-dev.10"
        initial_boot = identity["status"]["boot_id"]
        request("display.text", text="Metalio SDK dev.10\nDISPLAY TEST A")
        for command in ("inventory", "power.status", "imu.probe", "imu.read", "audio.info",
                        "audio.sample", "wifi.scan", "sd.roundtrip", "bt.info", "rtc.status"):
            result = request(command)
            if result.get("result") != "PASS":
                raise RuntimeError(f"{command}: {result}")
        if args.tones:
            print("Playing three 500ms tones at temporary volume 80", flush=True)
            for frequency in (440, 660, 880):
                request("audio.tone", duration_ms=500, frequency_hz=frequency, volume=80)
                deadline = time.monotonic() + 5
                while True:
                    status = request("status")
                    if not status["audio_tone_busy"]:
                        assert status["audio_tone_last_result"] == 0
                        assert status["audio_tone_samples"] == 8000
                        break
                    if time.monotonic() > deadline:
                        raise TimeoutError("Tone did not complete")
                    time.sleep(0.1)
                time.sleep(0.5)
        for invalid in (
            {"cmd": "audio.tone", "args": {"volume": 100}},
            {"cmd": "frame.read", "args": {"offset": 48000, "length": 1}},
            {"cmd": "scene.set", "args": {"scene": {"version": "invalid"}}},
        ):
            result = call(args.socket, invalid)
            journal.record("negative", {"request": invalid, "reply": result})
            assert result["ok"] is False
            request("ping")
        heaps = []
        for index in range(args.updates):
            scene = {"version": f"dev10-{index}", "rects": [
                {"x": 20 + index % 100, "y": 30, "w": 80, "h": 50, "black": True}]}
            request("scene.set", scene=scene)
            current = request("scene.get")
            assert current["scene_version"] == scene["version"]
            assert current["frame_sha256"] == sha(render_scene(scene))
            status = request("status")
            heaps.append(status["heap_free"])
            assert status["usb_stack_free_min_bytes"] >= 1024
            if (index + 1) % 20 == 0:
                print(f"PASS {index + 1} scene updates, boot_id={initial_boot}", flush=True)
            time.sleep(1.6)
        request("display.text", text="Metalio SDK dev.10\nTEST COMPLETE\nUSB service connected")
        # Verify full native framebuffer after the last scene (RAM scene is separate from text).
        expected = render_scene(scene)
        actual = bytearray()
        for offset in range(0, len(expected), 512):
            block = request("frame.read", offset=offset, length=min(512, len(expected)-offset))
            actual.extend(bytes.fromhex(block["hex"]))
        assert actual == expected
        journal.record("PASS", {"boot_id": initial_boot, "updates": args.updates,
            "heap_first": heaps[0], "heap_last": heaps[-1], "heap_min": min(heaps),
            "physical_display": "WAITING_USER", "physical_audio": "WAITING_USER"})
        print("PASS automated acceptance; physical display/audio require observation", flush=True)
    finally:
        journal.close()


if __name__ == "__main__":
    main()
