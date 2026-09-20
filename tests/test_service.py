import json
import socket
import sys
import threading
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from service import receive_line, Session


def test_device_log_export_is_incremental_and_rotates(tmp_path):
    names = [f"{i:016x}.jsonl" for i in range(6)]
    class Fake:
        commands = ["selftest.logs"]
        def request(self, command, **kwargs):
            if command == "selftest.logs":
                return {"files": names}
            data = b'{"event":"test"}\n'[kwargs["offset"]:]
            return {"hex": data.hex(), "bytes": len(data), "eof": True}
    session = Session(None, None)
    session.client = Fake()
    session.export_dir = tmp_path
    for _ in range(4):
        session.collect_logs()
    assert len(list(tmp_path.iterdir())) == 6
    assert all(p.read_bytes() == b'{"event":"test"}\n' for p in tmp_path.iterdir())


def test_device_log_export_rejects_path_escape(tmp_path):
    class Fake:
        commands = ["selftest.logs"]
        def request(self, command, **kwargs):
            return {"files": ["../../escape.jsonl"]}
    session = Session(None, None)
    session.client = Fake()
    session.export_dir = tmp_path
    with pytest.raises(ValueError):
        session.collect_logs()


@pytest.mark.parametrize("payload", [b"[]\n", b"bad\n", b"x" * 8193])
def test_local_protocol_rejects_invalid_input(payload):
    sender, receiver = socket.socketpair()
    try:
        writer = threading.Thread(target=sender.sendall, args=(payload,))
        writer.start()
        with pytest.raises(ValueError):
            receive_line(receiver)
        writer.join(timeout=2)
    finally:
        sender.close()
        receiver.close()


def test_reserved_arguments_cannot_override_wire_identity():
    session = Session(None, None)
    session.client = object()
    with pytest.raises(ValueError, match="Reserved"):
        session.execute({"cmd": "ping", "args": {"id": 77}})


def test_service_keeps_existing_client_between_calls():
    class Fake:
        def request(self, command, **kwargs):
            return {"boot_id": "unchanged", "command": command}
    session = Session(None, None)
    session.client = Fake()
    first = session.client
    for _ in range(3):
        assert session.execute({"cmd": "ping"})["boot_id"] == "unchanged"
    assert session.client is first


def test_reboot_rehandshake_preserves_transport(monkeypatch):
    import service
    transport=object()
    class Fake:
        def __init__(self,link,journal,expected_device):assert link is transport
        def hello(self):return {"device_id":"1020ba6e0be0","boot_id":"new"}
    monkeypatch.setattr(service,"Client",Fake)
    monkeypatch.setattr(service,"wait_until_ready",lambda client:None)
    monkeypatch.setattr(service,"open_serial",lambda port:pytest.fail("Must preserve transport"))
    session=Session(None,None);session.link=transport;session.client=object()
    session.invalidate_protocol();session.connect()
    assert session.link is transport and session.hello["boot_id"]=="new"


@pytest.mark.parametrize('during', ['hello', 'ready'])
def test_reboot_during_rehandshake_preserves_transport(monkeypatch, during):
    import service
    transport=object()
    def reboot():raise service.DeviceRebooted('test reboot')
    class Fake:
        def __init__(self,link,journal,expected_device):assert link is transport
        def hello(self):
            if during=='hello':reboot()
            return {'boot_id':'new'}
    monkeypatch.setattr(service,'Client',Fake)
    monkeypatch.setattr(service,'wait_until_ready',lambda client:reboot())
    session=Session(None,None);session.link=transport
    with pytest.raises(service.DeviceRebooted):session.connect()
    assert session.link is transport and session.client is None and session.hello is None
