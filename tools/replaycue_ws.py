#!/usr/bin/env python3
"""Send Replay Cue commands to OBS over obs-websocket (no third-party packages).

Examples:
  replaycue_ws.py GetStatus
  replaycue_ws.py InstantReplay '{"seconds": 10, "play": true}'
  replaycue_ws.py SetAngle '{"angle": 2}'
  replaycue_ws.py SetSpeed '{"percent": 50}'
  replaycue_ws.py obs:GetVersion              # any core obs-websocket request

Connection settings come from --host/--port/--password or the OBS_WS_HOST,
OBS_WS_PORT and OBS_WS_PASSWORD environment variables (default 127.0.0.1:4455).
"""

import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import uuid

VENDOR = "ReplayCue"
EVENT_VENDORS = 1 << 9


class ObsWebSocket:
    def __init__(self, host="127.0.0.1", port=4455, password=None, timeout=10.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            (
                f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
                f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: obswebsocket.json\r\n\r\n"
            ).encode()
        )
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("connection closed during handshake")
            self.buf += chunk
        head, _, self.buf = self.buf.partition(b"\r\n\r\n")
        if b" 101 " not in head.split(b"\r\n")[0]:
            raise ConnectionError(head.decode(errors="replace"))

        hello = self.recv()
        identify = {"rpcVersion": 1, "eventSubscriptions": EVENT_VENDORS}
        auth = hello["d"].get("authentication")
        if auth:
            if not password:
                raise PermissionError("OBS requires a websocket password")
            secret = base64.b64encode(hashlib.sha256((password + auth["salt"]).encode()).digest())
            identify["authentication"] = base64.b64encode(
                hashlib.sha256(secret + auth["challenge"].encode()).digest()
            ).decode()
        self.send({"op": 1, "d": identify})
        self.recv()  # Identified

    def close(self):
        self.sock.close()

    def send(self, obj):
        data = json.dumps(obj).encode()
        header = bytearray([0x81])
        mask = os.urandom(4)
        n = len(data)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        header += mask
        self.sock.sendall(bytes(header) + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("connection closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self):
        message = b""
        while True:
            b1, b2 = self._read(2)
            opcode = b1 & 0x0F
            n = b2 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            mask = self._read(4) if b2 & 0x80 else None
            payload = self._read(n)
            if mask:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
            if opcode == 8:
                raise ConnectionError("server closed the connection")
            if opcode == 9:  # ping
                self.sock.sendall(bytes([0x8A, 0x80]) + os.urandom(4))
                continue
            message += payload
            if b1 & 0x80:
                return json.loads(message)

    def request(self, request_type, data=None):
        rid = str(uuid.uuid4())
        self.send({"op": 6, "d": {"requestType": request_type, "requestId": rid, "requestData": data or {}}})
        while True:
            msg = self.recv()
            if msg.get("op") == 7 and msg["d"].get("requestId") == rid:
                status = msg["d"]["requestStatus"]
                if not status.get("result"):
                    raise RuntimeError(f"{request_type} failed: {status}")
                return msg["d"].get("responseData", {})

    def vendor(self, request_type, data=None):
        response = self.request(
            "CallVendorRequest", {"vendorName": VENDOR, "requestType": request_type, "requestData": data or {}}
        )
        return response.get("responseData", {})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("request", help="Replay Cue request name, or obs:<RequestType> for core requests")
    parser.add_argument("data", nargs="?", default="{}", help="JSON request data")
    parser.add_argument("--host", default=os.environ.get("OBS_WS_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("OBS_WS_PORT", "4455")))
    parser.add_argument("--password", default=os.environ.get("OBS_WS_PASSWORD"))
    args = parser.parse_args()

    ws = ObsWebSocket(args.host, args.port, args.password)
    try:
        data = json.loads(args.data)
        if args.request.startswith("obs:"):
            result = ws.request(args.request[4:], data)
        else:
            result = ws.vendor(args.request, data)
        json.dump(result, sys.stdout, indent=2)
        print()
    finally:
        ws.close()


if __name__ == "__main__":
    main()
