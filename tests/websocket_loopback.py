"""Exercise the production libcurl WebSocket owner with real loopback RFC6455 frames."""

import base64
import hashlib
import socket
import struct
import subprocess
import sys
import threading
import time


class LoopbackPeer:
    def __init__(self, mode):
        self.mode = mode
        self.error = None
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(5)
        self.port = self.listener.getsockname()[1]

    @staticmethod
    def exact(conn, size):
        data = b""
        while len(data) < size:
            part = conn.recv(size - len(data))
            if not part:
                raise EOFError("client closed before expected frame")
            data += part
        return data

    @classmethod
    def receive(cls, conn):
        head = cls.exact(conn, 2)
        opcode, length = head[0] & 15, head[1] & 127
        assert head[1] & 128, "client frames must be masked"
        if length == 126:
            length = struct.unpack("!H", cls.exact(conn, 2))[0]
        elif length == 127:
            length = struct.unpack("!Q", cls.exact(conn, 8))[0]
        assert length <= 4096
        mask = cls.exact(conn, 4)
        data = cls.exact(conn, length)
        return opcode, bytes(v ^ mask[i % 4] for i, v in enumerate(data))

    @staticmethod
    def send(conn, opcode, body, final=True):
        head = bytes([(128 if final else 0) | opcode])
        if len(body) < 126:
            head += bytes([len(body)])
        elif len(body) <= 65535:
            head += b"\x7e" + struct.pack("!H", len(body))
        else:
            head += b"\x7f" + struct.pack("!Q", len(body))
        conn.sendall(head + body)

    def run(self):
        try:
            with self.listener, self.listener.accept()[0] as conn:
                conn.settimeout(5)
                request = b""
                while not request.endswith(b"\r\n\r\n"):
                    request += self.exact(conn, 1)
                    assert len(request) < 16384
                if self.mode == "redirect":
                    conn.sendall(
                        b"HTTP/1.1 302 Found\r\nLocation: ws://127.0.0.1:1/\r\nContent-Length: 0\r\n\r\n"
                    )
                    return
                headers = dict(
                    line.split(b":", 1) for line in request.split(b"\r\n")[1:] if b":" in line
                )
                key = next(
                    v.strip() for k, v in headers.items() if k.lower() == b"sec-websocket-key"
                )
                accept = base64.b64encode(
                    hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest()
                )
                conn.sendall(
                    b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                    + accept
                    + b"\r\n\r\n"
                )
                assert self.receive(conn) == (1, b'{"hello":true}')
                opcode, token = self.receive(conn)
                assert opcode == 9 and token == b"loopback-challenge"
                if self.mode == "quiet":
                    time.sleep(0.3)
                elif self.mode == "close":
                    self.send(conn, 8, struct.pack("!H", 1000))
                elif self.mode == "oversize":
                    try:
                        self.send(conn, 2, b"x" * (1024 * 1024 + 1))
                    except (ConnectionResetError, BrokenPipeError):
                        pass
                else:
                    self.send(conn, 9, b"server-challenge")
                    self.send(conn, 2, b'{"message":', False)
                    self.send(conn, 10, b"unrelated-pong")
                    self.send(conn, 0, b'"ok"}')
                    assert self.receive(conn) == (10, b"server-challenge")
                    self.send(conn, 10, token)
        except Exception as exc:
            self.error = exc


def main():
    for mode in ["roundtrip", "quiet", "close", "oversize", "redirect"]:
        peer = LoopbackPeer(mode)
        thread = threading.Thread(target=peer.run, daemon=True)
        thread.start()
        run = subprocess.run(
            [sys.argv[1], str(peer.port), mode], capture_output=True, text=True, timeout=10
        )
        thread.join(timeout=6)
        assert not thread.is_alive() and peer.error is None, (mode, peer.error)
        assert run.returncode == 0, (mode, run.stdout, run.stderr)
        print(f"Real WebSocket wire passed: {mode}")


if __name__ == "__main__":
    main()
