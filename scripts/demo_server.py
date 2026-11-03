#!/usr/bin/env python3
"""Local server of the visual application (docs/APP.md).

    python scripts/demo_server.py [--port 8765] [--open]

Serves the built application (app/dist) at / and the scenario bundles (results/demo) at /data/.
Read-only, standard library only, bound to 127.0.0.1: nothing leaves the machine except the map
tiles the browser fetches from OpenStreetMap (switch "Fond de carte" off to work offline).
"""
from __future__ import annotations

import argparse
import functools
import http.server
import sys
import threading
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "app" / "dist"
DATA = ROOT / "results" / "demo"


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".json": "application/json; charset=utf-8",
                      ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8"}

    def translate_path(self, path: str) -> str:
        path = path.split("?", 1)[0].split("#", 1)[0]
        if path.startswith("/data/"):
            base, rel = DATA, path[len("/data/"):]
        else:
            base, rel = DIST, path.lstrip("/")
        target = (base / rel).resolve()
        if base.resolve() not in target.parents and target != base.resolve():
            return str(base / "__forbidden__")
        return str(target)

    def end_headers(self) -> None:
        if self.path.startswith("/data/"):
            self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt: str, *args) -> None:  # quiet: errors only
        if args and str(args[1]).startswith(("4", "5")):
            super().log_message(fmt, *args)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--open", action="store_true", help="open the browser")
    args = ap.parse_args()
    if not (DIST / "index.html").exists():
        sys.exit("app/dist is missing: build the application first (cd app && npm install && npm run build)")
    if not (DATA / "index.json").exists():
        sys.exit("results/demo/index.json is missing: run python scripts/build_demo.py first")
    if not (DIST / "client.html").exists():
        print("warning: app/dist has no client.html (built before the client mock-up): "
              "rebuild it with  cd app && npm run build", file=sys.stderr)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), functools.partial(Handler, directory=str(DIST)))
    url = f"http://127.0.0.1:{args.port}/"
    print(f"legalvrp demo: {url}  ·  client mock-up: {url}client.html  (Ctrl+C to stop)")
    if args.open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
