"""Validate retained USB while browsing a TXT file; never assert physical image quality."""
import json
from pathlib import Path
from inkdesk_acceptance import ask, app, wait, tap

hello=ask("hello")
boot=hello["boot_id"]
assert hello["version"]=="1.0.0-inkdesk-r2.2"
ask("inkdesk.open")
wait(lambda a:a["active"] and a["view"]==0)
tap(300,220,5)
wait(lambda a:a["reader"]["books"]>0)
tap(100,150,5)
first=app()
assert not first["reader"]["library"] and not first["reader"]["error"], first
if not first["reader"]["eof"]:
    tap(400,740,5)
    second=app()
    assert second["reader"]["offset"]==first["reader"]["next_offset"], second
    tap(80,740,5)
    assert app()["reader"]["offset"]==first["reader"]["offset"]
tap(230,740,5)
assert app()["reader"]["library"]
tap(230,740,0)
for _ in range(50):
    assert ask("ping")["boot_id"]==boot
result={"result":"PASS","identity":hello,"first_page":first,"final":app(),"physical_display":"NOT_PROVEN"}
path=Path(__file__).resolve().parents[1]/"artifacts/inkdesk-r2.2/reader-acceptance.json"
path.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")
print(json.dumps(result,ensure_ascii=False,indent=2))
