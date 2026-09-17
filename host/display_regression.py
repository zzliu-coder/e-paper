"""Verify the bounded display job path; physical refresh remains separate."""
from __future__ import annotations

import argparse
from pathlib import Path

from hardware_acceptance import wait_until_ready
from metalio import Client, Journal, open_serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()
    journal = Journal(Path(args.log), label="display_regression_dev9")
    link = open_serial(args.port)
    try:
        client = Client(link, journal, expected_device="1020ba6e0be0")
        hello = client.hello()
        if hello.get("version") != "1.0.0-dev.9":
            raise RuntimeError(f"unexpected firmware {hello.get('version')}")
        wait_until_ready(client)
        result = client.display_text("Metalio SDK dev.9")
        status = client.request("status")
        if result.get("job_result") != 0 or not status.get("board_ready"):
            raise RuntimeError(f"display job did not complete: result={result}, status={status}")
        journal.record("display_regression_pass", {
            "job_id": result.get("job_done"),
            "job_result": result.get("job_result"),
            "physical_refresh": "NOT_PROVEN",
            "boot_id": client.boot_id,
        })
        print("PASS display job; physical refresh remains NOT_PROVEN", flush=True)
    finally:
        link.close()
        journal.close()


if __name__ == "__main__":
    main()
