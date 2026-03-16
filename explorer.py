#!/usr/bin/env python3
"""HackNative Compiler Explorer — a Godbolt-like web UI for Hack → LLVM IR."""

import json
import os
import subprocess
import sys
import tempfile
from http.server import HTTPServer, BaseHTTPRequestHandler

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
HACKC = os.path.join(SCRIPT_DIR, "build", "hackc")
HTML_FILE = os.path.join(SCRIPT_DIR, "explorer.html")
PORT = 8080


def filter_ir(raw: str) -> str:
    """Filter IR output to keep only user-meaningful parts.

    Removes runtime 'declare' lines and the module header, keeps
    define blocks, vtable globals, type definitions, and user constants.
    """
    lines = raw.split("\n")
    filtered = []

    for line in lines:
        stripped = line.strip()
        # Skip module header lines
        if stripped.startswith("; ModuleID"):
            continue
        if stripped.startswith("source_filename"):
            continue
        # Skip runtime declare lines (external function declarations)
        if stripped.startswith("declare "):
            continue
        # Skip attributes lines
        if stripped.startswith("attributes #"):
            continue
        filtered.append(line)

    # Remove leading/trailing blank lines
    while filtered and not filtered[0].strip():
        filtered.pop(0)
    while filtered and not filtered[-1].strip():
        filtered.pop()

    # Collapse runs of 2+ blank lines into a single blank line
    result = []
    prev_blank = False
    for line in filtered:
        if not line.strip():
            if not prev_blank:
                result.append(line)
            prev_blank = True
        else:
            prev_blank = False
            result.append(line)

    return "\n".join(result)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        # Quieter logging: only errors
        if args and str(args[0]).startswith("4"):
            super().log_message(fmt, *args)

    def _send_json(self, data, status=200):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            try:
                with open(HTML_FILE, "rb") as f:
                    content = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)
            except FileNotFoundError:
                self.send_error(404, "explorer.html not found")
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path != "/api/compile":
            self.send_error(404)
            return

        content_len = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_len)

        try:
            req = json.loads(body)
        except json.JSONDecodeError:
            self._send_json({"error": "Invalid JSON"}, 400)
            return

        code = req.get("code", "")
        if not code.strip():
            self._send_json({"ir": "", "error": ""})
            return

        # Write code to a temp file
        try:
            fd, tmp_path = tempfile.mkstemp(suffix=".hack")
            with os.fdopen(fd, "w") as f:
                f.write(code)

            # Run hackc
            result = subprocess.run(
                [HACKC, tmp_path, "--dump-ir"],
                capture_output=True,
                text=True,
                timeout=10,
            )

            if result.returncode != 0:
                error_msg = result.stderr.strip() or result.stdout.strip()
                self._send_json({"ir": "", "error": error_msg})
            else:
                ir = result.stdout
                filtered = filter_ir(ir)
                self._send_json({"ir": filtered, "error": ""})

        except subprocess.TimeoutExpired:
            self._send_json({"ir": "", "error": "Compilation timed out (10s limit)"})
        except Exception as e:
            self._send_json({"ir": "", "error": str(e)}, 500)
        finally:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass


def main():
    if not os.path.isfile(HACKC):
        print(f"Error: hackc not found at {HACKC}")
        print("Please build hackc first:")
        print(f"  cd {SCRIPT_DIR} && mkdir -p build && cd build && cmake .. && make")
        sys.exit(1)

    server = HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"HackNative Compiler Explorer")
    print(f"  hackc : {HACKC}")
    print(f"  URL   : http://localhost:{PORT}")
    print(f"  Press Ctrl+C to stop")
    print()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.server_close()


if __name__ == "__main__":
    main()
