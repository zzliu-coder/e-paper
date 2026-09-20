#!/bin/bash
set -e
cd "$(dirname "$0")"
if [ ! -x .venv/bin/python ]; then
  python3 -m venv .venv
  .venv/bin/python -m pip install -r requirements.txt
fi
REPO=""
if [ -f ../../main/inkdesk_app.cc ]; then REPO="$(cd ../.. && pwd)"; fi
if [ -n "$REPO" ]; then exec .venv/bin/python app/serve.py --repo "$REPO"; fi
exec .venv/bin/python app/serve.py
