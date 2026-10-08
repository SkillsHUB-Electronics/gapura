#!/usr/bin/env python3
"""Command-line client for the Wireless RFID Reader (docs/PROTOCOL.md).

USB serial (needs `pip install pyserial`):
    python tools/rfid_cli.py --port COM5 ping
    python tools/rfid_cli.py --port COM5 read_block block=4
    python tools/rfid_cli.py --port COM5 write_block block=4 hex=00112233445566778899AABBCCDDEEFF
    python tools/rfid_cli.py --port COM5 config_set --json '{"led":{"brightness":40}}'
    python tools/rfid_cli.py --port COM5 monitor          # print events until Ctrl+C

Wi-Fi (Phase 4):
    python tools/rfid_cli.py --host gapura.local --token <token> info
    python tools/rfid_cli.py --host gapura.local --token <token> monitor   # needs `pip install websockets`

Arguments are key=value pairs; values are parsed as JSON when possible (block=4 -> 4).
"""

import argparse
import itertools
import json
import sys
import time
import urllib.request

_ids = itertools.count(1)


def parse_args(pairs, raw_json):
    if raw_json:
        return json.loads(raw_json)
    args = {}
    for pair in pairs:
        if "=" not in pair:
            sys.exit(f"argument must be key=value: {pair}")
        key, value = pair.split("=", 1)
        try:
            args[key] = json.loads(value)
        except json.JSONDecodeError:
            args[key] = value
    return args


def build_request(cmd, args):
    req = {"id": next(_ids), "cmd": cmd}
    if args:
        req["args"] = args
    return req


def print_json(obj):
    print(json.dumps(obj, indent=2, ensure_ascii=False))


class SerialClient:
    def __init__(self, port, baud):
        try:
            import serial  # pyserial
        except ImportError:
            sys.exit("pyserial is required: pip install pyserial")
        self.ser = serial.Serial(port, baud, timeout=0.2)
        time.sleep(0.2)
        self.ser.reset_input_buffer()

    def lines(self):
        buf = b""
        while True:
            chunk = self.ser.readline()
            if not chunk:
                yield None
                continue
            buf += chunk
            if buf.endswith(b"\n"):
                text = buf.decode("utf-8", "replace").strip()
                buf = b""
                if text:
                    yield text

    def request(self, req, timeout, verbose):
        self.ser.write((json.dumps(req) + "\n").encode())
        deadline = time.time() + timeout
        for text in self.lines():
            if time.time() > deadline:
                sys.exit("timeout waiting for response")
            if text is None:
                continue
            try:
                msg = json.loads(text)
            except json.JSONDecodeError:
                if verbose:
                    print(f"# {text}", file=sys.stderr)
                continue
            if msg.get("id") == req["id"] and "ok" in msg:
                return msg
            if verbose:
                print(f"# {text}", file=sys.stderr)

    def monitor(self):
        for text in self.lines():
            if text is not None:
                print(text, flush=True)


class HttpClient:
    def __init__(self, host, token):
        self.host = host
        self.token = token

    def request(self, req, timeout, verbose):
        http = urllib.request.Request(
            f"http://{self.host}/api/cmd",
            data=json.dumps(req).encode(),
            headers={"Content-Type": "application/json",
                     "Authorization": f"Bearer {self.token}"},
            method="POST",
        )
        with urllib.request.urlopen(http, timeout=timeout) as res:
            return json.loads(res.read())

    def monitor(self):
        try:
            import asyncio
            import websockets
        except ImportError:
            sys.exit("websockets is required: pip install websockets")

        async def run():
            async with websockets.connect(f"ws://{self.host}/ws?token={self.token}") as ws:
                async for text in ws:
                    print(text, flush=True)

        asyncio.run(run())


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    link = p.add_mutually_exclusive_group(required=True)
    link.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    link.add_argument("--host", help="device host, e.g. gapura.local or 192.168.1.50")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--token", default="", help="API token (Wi-Fi only)")
    p.add_argument("--timeout", type=float, default=15)
    p.add_argument("--json", help="args as a JSON object instead of key=value")
    p.add_argument("-v", "--verbose", action="store_true", help="show events while waiting")
    p.add_argument("cmd", help="protocol command, or 'monitor'")
    p.add_argument("args", nargs="*", help="key=value arguments")
    opts = p.parse_args()

    client = SerialClient(opts.port, opts.baud) if opts.port else HttpClient(opts.host, opts.token)
    try:
        if opts.cmd == "monitor":
            client.monitor()
            return
        res = client.request(build_request(opts.cmd, parse_args(opts.args, opts.json)),
                             opts.timeout, opts.verbose)
        print_json(res)
        sys.exit(0 if res.get("ok") else 1)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
