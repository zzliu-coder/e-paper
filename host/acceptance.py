"""Bounded device soak test. Does not flash or change control line states."""
import argparse
import time
from metalio import Client, Journal, choose_port, open_serial

def run(client, seconds, interval=2):
    hello = client.hello()
    required = {"hello", "ping", "status", "display.text", "job.get"}
    if not required <= set(hello.get("commands", [])):
        raise RuntimeError("Firmware lacks required diagnostic commands")
    start = time.monotonic()
    deadline = start + seconds
    samples = 0
    while time.monotonic() < deadline:
        client.request("ping", timeout=2)
        status = client.request("status", timeout=2)
        if not status.get("board_ready"):
            if time.monotonic() - start > 60:
                raise RuntimeError("Hardware initialization deadline exceeded")
        samples += 1
        time.sleep(interval)
    return {"samples": samples, "boot_id": hello["boot_id"], "physical_effect": "NOT_PROVEN"}

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port")
    parser.add_argument("--seconds", type=int, default=1800)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 28800:
        parser.error("seconds must be 1..28800")
    link = open_serial(choose_port(args.port))
    journal = Journal(args.log)
    try:
        print(run(Client(link, journal, expected_device="1020ba6e0be0"), args.seconds))
    finally:
        link.close()

