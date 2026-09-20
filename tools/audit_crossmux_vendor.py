"""Record exact local-vs-pinned source provenance; never fetch or touch hardware."""
import argparse,hashlib,json,subprocess
from pathlib import Path

PIN="7dcd8b19031c9277376c7e0f8cacda4d7b5a43a1"
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream",type=Path)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    vendor=Path(__file__).resolve().parents[1]/"components/paper_reader_crossmux/vendor"
    paths=set(subprocess.check_output(["git","-C",str(args.upstream),"ls-tree","-r","--name-only",PIN],text=True).splitlines())
    rows=[]
    for file in sorted(vendor.rglob("*")):
        if not file.is_file():continue
        rel=file.relative_to(vendor).as_posix();data=file.read_bytes()
        if b"Warning: truncated output" in data or b"tokens truncated" in data:raise ValueError("Truncated source: "+rel)
        row={"path":rel,"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()}
        if rel in paths:
            original=subprocess.check_output(["git","-C",str(args.upstream),"show",PIN+":"+rel])
            row.update(upstream_sha256=hashlib.sha256(original).hexdigest(),modified=original!=data)
        else:row["local_adapter_or_notice"]=True
        rows.append(row)
    result={"source":"https://github.com/0x1abin/crossmux","commit":PIN,"files":rows}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open("x") as out:json.dump(result,out,ensure_ascii=False,indent=2);out.write("\n")
    print(json.dumps({"files":len(rows),"modified":sum(r.get("modified",False) for r in rows),"report":str(args.out)}))
if __name__=="__main__":main()
