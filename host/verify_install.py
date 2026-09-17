"""Record local readback evidence separately from on-device hardware acceptance."""
import hashlib
import json
import sys
from pathlib import Path

directory = Path(sys.argv[1]).resolve()
plan = json.loads((directory / "install-plan.json").read_text())
firmware = (directory / "xiaozhi.bin").read_bytes()
readback = (directory / "readback.bin").read_bytes()
digest = hashlib.sha256(firmware).hexdigest()
assert firmware == readback
assert digest == plan["write_segments"][0]["sha256"]
receipt = {"candidate": plan["candidate"], "app_offset": "0x80000",
           "bytes": len(firmware), "sha256": digest, "readback": "PASS",
           "physical_acceptance": "SEPARATE_EVIDENCE_REQUIRED",
           "full_recovery_exercised": False}
(directory / "install-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt))
