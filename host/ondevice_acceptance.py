"""Exercise on-device test paths without fabricating physical confirmations."""
import json
import time
from pathlib import Path
from service import call, DEFAULT_SOCKET


def request(command, **args):
    reply = call(DEFAULT_SOCKET, {"cmd": command, "args": args})
    if not reply.get("ok"):
        raise RuntimeError(reply)
    return reply["result"]


def main():
    out = Path(__file__).resolve().parents[1] / "artifacts/dev13/ondevice-acceptance.jsonl"
    with out.open("a") as log:
        def save(kind, value):
            log.write(json.dumps({"event": kind, "data": value}, ensure_ascii=False) + "\n")
            log.flush()
            print(kind, json.dumps(value, ensure_ascii=False), flush=True)
        identity = request("hello")
        assert identity["version"] == "1.0.0-dev.13"
        boot = identity["boot_id"]
        save("identity", identity)
        for module in ("sd", "rtc", "recovery", "wifi", "bluetooth", "display", "speaker"):
            request("selftest.open", module=module)
            time.sleep(1)
            request("selftest.run", module=module)
            deadline = time.monotonic() + 65
            while True:
                time.sleep(1)
                result = request("selftest.status")
                assert result["boot_id"] == boot, "Unexpected reboot"
                if not result["busy"]:
                    save(module, result)
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError(module)
        request("selftest.open")
        save("final", request("status"))


if __name__ == "__main__":
    main()
