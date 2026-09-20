from pathlib import Path
import subprocess,sys
repo=Path(__file__).resolve().parents[1]
raise SystemExit(subprocess.call([sys.executable,str(repo/'tools/fontbench/app/serve.py'),'--repo',str(repo)]))
