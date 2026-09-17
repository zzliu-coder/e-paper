"""Device negative-path checks. Only exclusive unfinished test files are created/aborted."""
import hashlib
import json
from pathlib import Path
import uuid
from inkdesk_acceptance import ask

results=[]
def reject(command,error,**args):
    try:ask(command,**args)
    except RuntimeError as exc:
        assert error in str(exc),(error,exc)
        results.append({"case":error,"result":"PASS"})
    else:raise AssertionError("Unexpected command success: "+command)

hello=ask("hello")
name="sdk-transfer-check-"+uuid.uuid4().hex[:8]+".txt"
reject("book.begin","invalid_book_manifest",name="../escape.epub",size=1,sha256="0"*64)
reject("book.begin","invalid_book_manifest",name=name,size=16777217,sha256="0"*64)
reject("book.read","invalid_book_read",name="../state.a",offset=0)
t=ask("book.begin",name=name,size=1,sha256="0"*64)["token"]
try:
    reject("book.begin","transfer_busy",name=name,size=1,sha256="0"*64)
    reject("book.chunk","unknown_transfer",token="wrong",offset=0,hex="61")
    reject("book.chunk","invalid_chunk_offset_or_size",token=t,offset=1,hex="61")
    reject("book.chunk","invalid_hex",token=t,offset=0,hex="zz")
    reject("book.commit","incomplete_transfer",token=t)
    ask("book.chunk",token=t,offset=0,hex="61")
    reject("book.commit","sha256_mismatch",token=t)
finally:
    try:ask("book.abort",token=t)
    except RuntimeError:pass
reject("book.read","book_unavailable",name=name,offset=0)
t=ask("book.begin",name=name,size=1,sha256=hashlib.sha256(b"a").hexdigest())["token"]
ask("book.abort",token=t)
reject("book.read","book_unavailable",name=name,offset=0)
assert ask("ping")["boot_id"]==hello["boot_id"]
report={"result":"PASS","boot_id":hello["boot_id"],"checks":results}
path=Path(__file__).resolve().parents[1]/"artifacts"/hello["version"].removeprefix("1.0.0-")/"transfer-negative-checks.json"
path.write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
