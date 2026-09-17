import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
import hardware_acceptance


class ProbeFake:
    def __init__(self):
        self.physical_calls = []

    def hello(self):
        return {
            "device_id": "1020ba6e0be0",
            "boot_id": "probe-session",
            "version": "1.0.0-dev.4",
            "hardware_probe": "dev.4",
            "efuse_write": False,
            "flash_write": False,
            "commands": ["hello", "ping", "status", *hardware_acceptance.READ_ONLY_COMMANDS],
        }

    def request(self, command, timeout=3):
        if command == "status":
            return {"ok": True, "board_ready": True, "audio_tone_busy": False}
        return {"ok": True, "result": "PASS", "command": command}

    def inventory(self):
        return self.request("inventory")

    def input_snapshot(self):
        return self.request("input.snapshot")

    def power_status(self):
        return self.request("power.status")

    def probe_accelerometer(self):
        return self.request("imu.probe")

    def read_accelerometer(self):
        return self.request("imu.read")

    def audio_info(self):
        return self.request("audio.info")

    def audio_sample(self):
        return self.request("audio.sample")

    def wifi_status(self):
        return self.request("wifi.status")

    def wifi_scan(self):
        return self.request("wifi.scan")

    def sd_status(self):
        return self.request("sd.status")

    def sd_roundtrip(self):
        return self.request("sd.roundtrip")

    def bluetooth_info(self):
        return self.request("bt.info")

    def rtc_status(self):
        return self.request("rtc.status")

    def pulse_haptic(self):
        self.physical_calls.append("haptic.pulse")
        return self.request("haptic.pulse")

    def play_tone(self):
        self.physical_calls.append("audio.tone")
        return self.request("audio.tone")

    def wait_tone_idle(self, timeout=3):
        return self.request("status")

    def receive(self):
        return []


class HardwareAcceptanceTests(unittest.TestCase):
    def test_default_probe_does_not_request_physical_actions(self):
        fake = ProbeFake()
        result = hardware_acceptance.run(fake)
        self.assertEqual(result["summary"]["protocol_failures"], 0)
        self.assertEqual(fake.physical_calls, [])
        self.assertEqual(result["identity"]["state"], "PASS")


if __name__ == "__main__":
    unittest.main()
