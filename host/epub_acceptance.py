"""Open the transferred EPUB, cross chapters and return; leave a text page visible."""
import json
from pathlib import Path
from inkdesk_acceptance import ask,app,wait,tap

hello=ask("hello");boot=hello["boot_id"]
ask("inkdesk.open")
wait(lambda a:a["active"] and a["view"]==0)
tap(300,220,5)
wait(lambda a:a["reader"]["books"]>0)
# This fixture is installed into an otherwise empty /books; verify selected book.
tap(100,150,5)
first=app()
assert first["reader"]["format"]=="EPUB" and first["reader"]["loaded"],first
assert "系统之美" in first["reader"]["title"],first
pages=[first["reader"]]
for _ in range(8):
    a=tap(400,740,5)
    assert a["reader"]["loaded"] and not a["reader"]["error"],a
    assert a["reader"]["page"]==pages[-1]["page"]+1,a
    pages.append(a["reader"])
assert pages[-1]["chapter"]>pages[0]["chapter"],pages
for _ in range(8):tap(80,740,5)
back=app()["reader"]
assert (back["page"],back["chapter"],back["offset"])==(first["reader"]["page"],first["reader"]["chapter"],first["reader"]["offset"])
# Skip cover/front matter and leave page 5 at the beginning of a longer text chapter.
for _ in range(4):tap(400,740,5)
for _ in range(40):assert ask("ping")["boot_id"]==boot
report={"result":"PASS","boot_id":boot,"tested_pages":pages,"final":app(),"device":ask("status"),"physical_display":"AWAITING_USER"}
path=Path(__file__).resolve().parents[1]/"artifacts"/hello["version"].removeprefix("1.0.0-")/"epub-acceptance.json"
path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
print(json.dumps(report,ensure_ascii=False,indent=2))
