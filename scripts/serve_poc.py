#!/usr/bin/env python3
"""Private Tailscale-only PoC server: APK/map download + screenshot upload,
and diagnostics reports from the app's SEND REPORT (POST /report, JSON):
they land in <root>/reports/ (latest.json is the newest) for an agent to read.

Binds only to the given address (Tailscale IP). Never listens on 0.0.0.0.
Uploads land in <root>/uploads/ and are also mirrored to --mirror if set.
"""
from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote

MAX_BYTES = 20 * 1024 * 1024
MAX_REPORT_BYTES = 4 * 1024 * 1024
ALLOWED_EXT = {".jpg", ".jpeg", ".png", ".webp", ".heic", ".gif"}
NAME_OK = re.compile(r"[^A-Za-z0-9._-]+")


def safe_name(name: str) -> str:
    base = os.path.basename(name.replace("\\", "/"))
    stem, ext = os.path.splitext(base)
    ext = ext.lower()
    if ext not in ALLOWED_EXT:
        ext = ".jpg"
    stem = NAME_OK.sub("_", stem)[:80] or "shot"
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    return f"{stamp}-{stem}{ext}"


def parse_multipart(content_type: str, body: bytes) -> tuple[str, bytes] | None:
    m = re.search(r'boundary=(?:"([^"]+)"|([^;]+))', content_type, re.I)
    if not m:
        return None
    boundary = (m.group(1) or m.group(2)).encode("ascii", "ignore")
    if not boundary:
        return None
    for part in body.split(b"--" + boundary):
        if b"Content-Disposition" not in part:
            continue
        header, sep, data = part.partition(b"\r\n\r\n")
        if not sep:
            continue
        if data.endswith(b"\r\n"):
            data = data[:-2]
        if data.endswith(b"--"):
            data = data[:-2]
        hm = re.search(br'filename="([^"]*)"', header)
        name = hm.group(1).decode("utf-8", "replace") if hm else "shot.jpg"
        if not data:
            continue
        return name, data
    return None


class Handler(BaseHTTPRequestHandler):
    root: Path
    mirror: Path | None

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send(self, code: int, body: bytes, ctype: str = "text/plain; charset=utf-8") -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        path = unquote(self.path.split("?", 1)[0])
        if path == "/uploads" or path == "/uploads/":
            up = self.root / "uploads"
            names = sorted(p.name for p in up.glob("*") if p.is_file()) if up.is_dir() else []
            body = ("\n".join(names) + ("\n" if names else "")).encode()
            self._send(200, body)
            return
        if path == "/":
            path = "/index.html"
        rel = path.lstrip("/")
        if ".." in rel.split("/"):
            self._send(400, b"bad path")
            return
        target = (self.root / rel).resolve()
        try:
            target.relative_to(self.root.resolve())
        except ValueError:
            self._send(400, b"bad path")
            return
        if not target.is_file():
            self._send(404, b"not found")
            return
        ctype = "application/octet-stream"
        if target.suffix == ".html":
            ctype = "text/html; charset=utf-8"
        elif target.suffix == ".map":
            ctype = "application/octet-stream"
        elif target.suffix == ".apk":
            ctype = "application/vnd.android.package-archive"
        data = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Content-Disposition", f'inline; filename="{target.name}"')
        self.end_headers()
        self.wfile.write(data)

    def _report(self) -> None:
        try:
            n = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            n = 0
        if n <= 0 or n > MAX_REPORT_BYTES:
            self._send(413, b"report too large (4 MB max)")
            return
        code, msg = save_report(self.root, self.rfile.read(n), self.mirror)
        sys.stderr.write(msg + "\n")
        self._send(code, (msg + "\n").encode())

    def do_POST(self) -> None:
        path = self.path.split("?", 1)[0]
        if path == "/report":
            self._report()
            return
        if path != "/upload":
            self._send(404, b"not found")
            return
        try:
            n = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            n = 0
        if n <= 0 or n > MAX_BYTES:
            self._send(413, b"file too large (20 MB max)")
            return
        body = self.rfile.read(n)
        ctype = self.headers.get("Content-Type", "")
        parsed = parse_multipart(ctype, body)
        if parsed:
            orig, data = parsed
        else:
            orig = self.headers.get("X-Filename", "shot.jpg")
            data = body
        if not data:
            self._send(400, b"empty file")
            return
        if len(data) > MAX_BYTES:
            self._send(413, b"file too large (20 MB max)")
            return
        name = safe_name(orig)
        dest_dir = self.root / "uploads"
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest = dest_dir / name
        dest.write_bytes(data)
        if self.mirror:
            self.mirror.mkdir(parents=True, exist_ok=True)
            (self.mirror / name).write_bytes(data)
        msg = f"saved {name} ({len(data)} bytes)\n"
        sys.stderr.write(msg)
        self._send(200, msg.encode())


def save_report(root: Path, body: bytes, mirror: Path | None = None) -> tuple[int, str]:
    """Stores one SEND REPORT upload. Returns (HTTP status, message)."""
    import json
    if not body or len(body) > MAX_REPORT_BYTES:
        return 413, "report empty or over 4 MB"
    try:
        doc = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as e:
        return 400, f"not JSON: {e}"
    if not isinstance(doc, dict) or doc.get("kind") != "megamod-report":
        return 400, "not a megamod report"
    build = NAME_OK.sub("_", str(doc.get("build", {}).get("version", "unknown")))[:40]
    reason = NAME_OK.sub("_", str(doc.get("reason", "report")))[:16]
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    name = f"{stamp}-{reason}-{build}.json"
    for base in (root, mirror):
        if base is None:
            continue
        d = base / "reports"
        d.mkdir(parents=True, exist_ok=True)
        (d / name).write_bytes(body)
        (d / "latest.json").write_bytes(body)
    return 200, f"saved report {name} ({len(body)} bytes)"


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--root", required=True)
    p.add_argument("--bind", default="100.89.1.14")
    p.add_argument("--port", type=int, default=8731)
    p.add_argument("--mirror", default="")
    args = p.parse_args()
    Handler.root = Path(args.root).resolve()
    Handler.mirror = Path(args.mirror).resolve() if args.mirror else None
    httpd = ThreadingHTTPServer((args.bind, args.port), Handler)
    print(f"serving {Handler.root} on {args.bind}:{args.port}", flush=True)
    httpd.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
