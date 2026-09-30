# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A minimal Chrome DevTools Protocol client over WebSocket (RFC 6455).

The tools use the standard library only. DevTools' HTTP endpoints (/json/*)
open and close tabs, but typing into a page needs the protocol itself, which
runs over a WebSocket. This implements the part of RFC 6455 a DevTools client
needs: the client handshake, masked text frames out, and text, fragmented,
ping and close frames in.
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import socket
import struct
import urllib.parse

OP_CONTINUATION, OP_TEXT, OP_BINARY = 0x0, 0x1, 0x2
OP_CLOSE, OP_PING, OP_PONG = 0x8, 0x9, 0xA
_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class ProtocolError(RuntimeError):
    """DevTools answered a command with an error."""


def accept_key(key: str) -> str:
    """The Sec-WebSocket-Accept value a server must answer `key` with."""
    return base64.b64encode(hashlib.sha1((key + _GUID).encode("ascii")).digest()).decode("ascii")


def encode_frame(payload: bytes, opcode: int = OP_TEXT, mask: bytes | None = b"") -> bytes:
    """Encodes one final frame.

    Clients must mask every frame with a fresh key, which the default does;
    `mask=None` encodes an unmasked frame, as servers send them.
    """
    if mask == b"":
        mask = os.urandom(4)
    length = len(payload)
    mask_bit = 0x80 if mask is not None else 0
    header = bytes([0x80 | opcode])
    if length < 126:
        header += bytes([mask_bit | length])
    elif length < 1 << 16:
        header += bytes([mask_bit | 126]) + struct.pack("!H", length)
    else:
        header += bytes([mask_bit | 127]) + struct.pack("!Q", length)
    if mask is None:
        return header + payload
    return header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


class WebSocket:
    def __init__(self, sock: socket.socket):
        self.sock = sock

    @classmethod
    def connect(cls, url: str, timeout: float = 10) -> "WebSocket":
        parts = urllib.parse.urlsplit(url)
        if parts.scheme != "ws":
            raise ValueError(f"only ws:// URLs are supported: {url}")
        sock = socket.create_connection((parts.hostname, parts.port or 80), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        path = parts.path + (f"?{parts.query}" if parts.query else "")
        # No Origin header: DevTools rejects connections that send one unless
        # the browser was started with --remote-allow-origins.
        sock.sendall((f"GET {path or '/'} HTTP/1.1\r\nHost: {parts.netloc}\r\n"
                      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                      f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
                     .encode("ascii"))
        ws = cls(sock)
        response = b""
        while b"\r\n\r\n" not in response:
            response += ws._recv_exactly(1)
        head = response.decode("latin-1").split("\r\n")
        headers = {k.strip().lower(): v.strip()
                   for k, _, v in (line.partition(":") for line in head[1:] if line)}
        if " 101 " not in head[0] + " ":
            sock.close()
            raise ConnectionError(f"WebSocket handshake refused: {head[0]}")
        if headers.get("sec-websocket-accept") != accept_key(key):
            sock.close()
            raise ConnectionError("WebSocket handshake: wrong Sec-WebSocket-Accept")
        return ws

    def _recv_exactly(self, n: int) -> bytes:
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("WebSocket peer closed the connection")
            data += chunk
        return data

    def read_frame(self) -> tuple[int, bool, bytes]:
        """Returns (opcode, fin, payload) of the next frame, unmasked."""
        b0, b1 = self._recv_exactly(2)
        length = b1 & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._recv_exactly(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._recv_exactly(8))[0]
        mask = self._recv_exactly(4) if b1 & 0x80 else None
        payload = self._recv_exactly(length)
        if mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        return b0 & 0x0F, bool(b0 & 0x80), payload

    def recv_message(self) -> str:
        """Returns the next text message, answering pings on the way."""
        parts: list[bytes] = []
        while True:
            opcode, fin, payload = self.read_frame()
            if opcode == OP_PING:
                self.sock.sendall(encode_frame(payload, OP_PONG))
                continue
            if opcode == OP_PONG:
                continue
            if opcode == OP_CLOSE:
                raise ConnectionError("WebSocket closed by the peer")
            parts.append(payload)
            if fin:
                return b"".join(parts).decode("utf-8")

    def send_text(self, text: str) -> None:
        self.sock.sendall(encode_frame(text.encode("utf-8")))

    def close(self) -> None:
        try:
            self.sock.sendall(encode_frame(b"", OP_CLOSE))
        except OSError:
            pass
        self.sock.close()


class Session:
    """Commands and events of one DevTools target."""

    def __init__(self, ws: WebSocket):
        self.ws = ws
        self.events: list[dict] = []
        self._next_id = 0

    @classmethod
    def connect(cls, websocket_url: str, timeout: float = 30) -> "Session":
        return cls(WebSocket.connect(websocket_url, timeout))

    def call(self, method: str, **params) -> dict:
        self._next_id += 1
        self.ws.send_text(json.dumps({"id": self._next_id, "method": method, "params": params}))
        while True:
            message = json.loads(self.ws.recv_message())
            if "id" not in message:
                self.events.append(message)
            elif message["id"] == self._next_id:
                if "error" in message:
                    raise ProtocolError(f"{method}: {message['error'].get('message')}")
                return message.get("result", {})

    def close(self) -> None:
        self.ws.close()
