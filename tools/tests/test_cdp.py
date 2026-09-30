# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import socket
import threading
import unittest

import cdp

# Examples from RFC 6455, section 5.7.
HELLO_UNMASKED = bytes([0x81, 0x05]) + b"Hello"
HELLO_MASKED = bytes([0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58])
HELLO_FRAGMENTED = bytes([0x01, 0x03]) + b"Hel" + bytes([0x80, 0x02]) + b"lo"
PING_HELLO = bytes([0x89, 0x05]) + b"Hello"


def server_frame(text: str) -> bytes:
    """A text frame as a server sends it: unmasked."""
    return cdp.encode_frame(text.encode("utf-8"), mask=None)


class FrameTest(unittest.TestCase):
    def test_accept_key(self):
        # RFC 6455, section 1.3.
        self.assertEqual(cdp.accept_key("dGhlIHNhbXBsZSBub25jZQ=="),
                         "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

    def test_masked_text_frame(self):
        self.assertEqual(cdp.encode_frame(b"Hello", mask=bytes([0x37, 0xFA, 0x21, 0x3D])),
                         HELLO_MASKED)

    def test_unmasked_text_frame(self):
        self.assertEqual(cdp.encode_frame(b"Hello", mask=None), HELLO_UNMASKED)

    def test_extended_payload_lengths(self):
        self.assertEqual(cdp.encode_frame(b"x" * 256, mask=None)[:4],
                         bytes([0x81, 0x7E, 0x01, 0x00]))
        self.assertEqual(cdp.encode_frame(b"x" * 65536, mask=None)[:10],
                         bytes([0x81, 0x7F, 0, 0, 0, 0, 0, 1, 0, 0]))

    def test_client_frames_are_masked_with_a_fresh_key(self):
        a, b = cdp.encode_frame(b"Hello"), cdp.encode_frame(b"Hello")
        self.assertEqual(a[1], 0x85)
        self.assertNotEqual(a[2:6], b[2:6])


class SocketPairTest(unittest.TestCase):
    def setUp(self):
        self.client_sock, self.server = socket.socketpair()
        # A frame that never comes fails the test instead of hanging it.
        self.client_sock.settimeout(5)
        self.server.settimeout(5)
        self.addCleanup(self.server.close)
        self.ws = cdp.WebSocket(self.client_sock)
        self.addCleanup(self.ws.close)

    def recv_client_frame(self) -> bytes:
        """Reads one masked client frame on the server side and unmasks it."""
        reader = cdp.WebSocket(self.server)
        opcode, fin, payload = reader.read_frame()
        self.assertTrue(fin)
        return payload


class WebSocketTest(SocketPairTest):
    def test_reads_a_single_frame_message(self):
        self.server.sendall(HELLO_UNMASKED)
        self.assertEqual(self.ws.recv_message(), "Hello")

    def test_reads_a_masked_frame(self):
        self.server.sendall(HELLO_MASKED)
        self.assertEqual(self.ws.recv_message(), "Hello")

    def test_assembles_fragments(self):
        self.server.sendall(HELLO_FRAGMENTED)
        self.assertEqual(self.ws.recv_message(), "Hello")

    def test_reads_extended_lengths(self):
        self.server.sendall(server_frame("y" * 70000))
        self.assertEqual(len(self.ws.recv_message()), 70000)

    def test_answers_ping_with_pong(self):
        self.server.sendall(PING_HELLO + HELLO_UNMASKED)
        self.assertEqual(self.ws.recv_message(), "Hello")
        opcode, fin, payload = cdp.WebSocket(self.server).read_frame()
        self.assertEqual((opcode, payload), (cdp.OP_PONG, b"Hello"))

    def test_close_frame_ends_the_connection(self):
        self.server.sendall(bytes([0x88, 0x00]))
        with self.assertRaises(ConnectionError):
            self.ws.recv_message()

    def test_peer_hanging_up_ends_the_connection(self):
        self.server.close()
        with self.assertRaises(ConnectionError):
            self.ws.recv_message()

    def test_sends_masked_text(self):
        self.ws.send_text("Hello")
        self.assertEqual(self.recv_client_frame(), b"Hello")


class SessionTest(SocketPairTest):
    def serve(self, *replies_for_request):
        """Answers the next request with each of `replies_for_request`, formatted with its id."""
        def run():
            request = json.loads(self.recv_client_frame())
            self.requests.append(request)
            for reply in replies_for_request:
                self.server.sendall(server_frame(json.dumps(reply(request["id"]))))

        self.requests = []
        thread = threading.Thread(target=run)
        thread.start()
        return thread

    def test_call_returns_the_result_matching_its_id(self):
        thread = self.serve(
            lambda i: {"method": "Page.loadEventFired", "params": {"timestamp": 1}},
            lambda i: {"id": i + 1000, "result": {"stale": True}},
            lambda i: {"id": i, "result": {"frameId": "F"}})
        session = cdp.Session(self.ws)
        self.assertEqual(session.call("Page.navigate", url="http://127.0.0.1/"), {"frameId": "F"})
        thread.join()
        self.assertEqual(self.requests[0]["method"], "Page.navigate")
        self.assertEqual(self.requests[0]["params"], {"url": "http://127.0.0.1/"})
        self.assertEqual([e["method"] for e in session.events], ["Page.loadEventFired"])

    def test_call_raises_protocol_errors(self):
        thread = self.serve(lambda i: {"id": i, "error": {"code": -32601,
                                                          "message": "'X.y' wasn't found"}})
        with self.assertRaisesRegex(cdp.ProtocolError, "wasn't found"):
            cdp.Session(self.ws).call("X.y")
        thread.join()


class HandshakeTest(unittest.TestCase):
    def serve_handshake(self, accept_for):
        listener = socket.create_server(("127.0.0.1", 0))
        self.addCleanup(listener.close)
        self.request = b""

        def run():
            conn, _ = listener.accept()
            with conn:
                while b"\r\n\r\n" not in self.request:
                    self.request += conn.recv(4096)
                key = next(line.split(":", 1)[1].strip()
                           for line in self.request.decode().split("\r\n")
                           if line.lower().startswith("sec-websocket-key:"))
                conn.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                             b"Connection: Upgrade\r\nSec-WebSocket-Accept: "
                             + accept_for(key).encode() + b"\r\n\r\n" + HELLO_UNMASKED)

        thread = threading.Thread(target=run)
        thread.start()
        self.addCleanup(thread.join)
        return listener.getsockname()[1]

    def test_connects_without_an_origin_header(self):
        # DevTools refuses WebSocket connections that carry an Origin unless
        # the browser runs with --remote-allow-origins.
        port = self.serve_handshake(cdp.accept_key)
        ws = cdp.WebSocket.connect(f"ws://127.0.0.1:{port}/devtools/page/ABC")
        self.addCleanup(ws.close)
        self.assertEqual(ws.recv_message(), "Hello")
        self.assertTrue(self.request.startswith(b"GET /devtools/page/ABC HTTP/1.1\r\n"))
        self.assertNotIn(b"origin:", self.request.lower())

    def test_rejects_a_wrong_accept_key(self):
        port = self.serve_handshake(lambda key: "wrong")
        with self.assertRaisesRegex(ConnectionError, "Sec-WebSocket-Accept"):
            cdp.WebSocket.connect(f"ws://127.0.0.1:{port}/devtools/page/ABC")


if __name__ == "__main__":
    unittest.main()
