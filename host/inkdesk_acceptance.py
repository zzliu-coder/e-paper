"""On-device application + retained USB smoke checks. Leaves one named demo note."""
import json
import time
from pathlib import Path
from service import call, DEFAULT_SOCKET


def ask(command, **args):
    r=call(DEFAULT_SOCKET,{"cmd":command,"args":args})
    if not r.get("ok"):
        raise RuntimeError(r)
    return r["result"]


def app():
    return ask("inkdesk.status")["app"]


def wait(check, seconds=15):
    end=time.monotonic()+seconds
    p={"error":"app_starting"}
    while time.monotonic()<end:
        try:
            p=app()
        except RuntimeError as exc:
            if "app_starting" not in str(exc):
                raise
            time.sleep(.25)
            continue
        assert not p["refresh_fault"], p
        if check(p):
            return p
        time.sleep(.25)
    raise TimeoutError(p)


def tap(x,y,view=None):
    before=app()
    ask("inkdesk.tap",x=x,y=y)
    return wait(lambda a:a["frames"]>before["frames"] and (view is None or a["view"]==view))


if __name__=="__main__":
    hello=ask("hello")
    boot=hello["boot_id"]
    ask("inkdesk.open")
    a=wait(lambda a:a["active"])
    if a["view"]!=0:
        # R2 Notes page back button.
        tap(400,95,0)
    tap(100,220,1)
    created=False
    if app()["notes"]==0:
        tap(200,195,2)
        tap(50,733)
        for ch in "sdk r2 check":
            ask("inkdesk.key",key=ch)
            time.sleep(.12)
        wait(lambda a:a["journal_sequence"]>0 and not a["dirty"],20)
        tap(300,95,1)
        created=True
    tap(400,95,0)
    ask("selftest.open")
    wait(lambda a:not a["active"])
    ask("inkdesk.open")
    wait(lambda a:a["active"] and not a["refresh_fault"])
    for _ in range(30):
        assert ask("ping")["boot_id"]==boot
    report={"result":"PASS","boot_id":boot,"created_demo_note":created,"app":app(),"device":ask("status")}
    version=hello["version"].removeprefix("1.0.0-")
    path=Path(__file__).resolve().parents[1]/"artifacts"/version/"application-acceptance.json"
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(json.dumps(report,ensure_ascii=False,indent=2))
