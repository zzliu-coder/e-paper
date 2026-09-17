import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
import acceptance
from metalio import Client, Decoder, render_scene, validate_scene

class Fake:
    def __init__(self, commands):
        self.commands=commands
    def hello(self):
        return {"commands":self.commands,"boot_id":"session"}
    def request(self, command, timeout):
        return {"board_ready":True}

class AcceptanceTests(unittest.TestCase):
    def test_oversize_frame_recovers(self):
        decoder=Decoder()
        events=decoder.feed(b"x"*17000+b'\nML1 {"protocol":1,"ok":true}\n')
        self.assertEqual(events[0][0],"framing_error")
        self.assertEqual(events[1][0],"packet")
    def test_capability_gate(self):
        client=Client(None,None)
        client.device_id="test"
        client.commands={"ping"}
        with self.assertRaises(ValueError):
            client.request("display.text",text="test")
    def test_text_limit_before_transport(self):
        client=Client(None,None)
        with self.assertRaises(ValueError):
            client.display_text("中"*100)
    def test_rejects_product_firmware(self):
        with self.assertRaises(RuntimeError):
            acceptance.run(Fake([]), 1)
    def test_runs_bounded_session(self):
        with patch("acceptance.time.monotonic",side_effect=[0,0,1,3]), patch("acceptance.time.sleep"):
            result=acceptance.run(Fake(["hello","ping","status","display.text","job.get"]),2)
        self.assertEqual(result["samples"],2)
        self.assertEqual(result["physical_effect"],"NOT_PROVEN")

    def test_scene_frame_is_native_800x480_msb(self):
        scene = {"version": "unit", "rects": [{"x": 0, "y": 0, "w": 1, "h": 1, "black": True}]}
        frame = render_scene(scene)
        self.assertEqual(len(frame), 48000)
        self.assertEqual(frame[0] & 0x80, 0)
        self.assertEqual(frame[0] & 0x40, 0x40)
        self.assertEqual(frame[100], 0xff)

    def test_scene_rejects_unknown_fields_and_out_of_bounds_rect(self):
        with self.assertRaises(ValueError):
            validate_scene({"version": "unit", "rects": [], "extra": 1})
        with self.assertRaises(ValueError):
            validate_scene({"version": "unit", "rects": [{"x": 799, "y": 0, "w": 2, "h": 1, "black": True}]})

if __name__=="__main__":
    unittest.main()
