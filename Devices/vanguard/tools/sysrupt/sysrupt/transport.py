# SPDX-License-Identifier: Apache-2.0
"""Shared network transport: a small TCP client and a UDP client.

Design goals (the ESP32-S3 badge has ~3.4 KB free heap and a tiny network stack):
  * one connection at a time per client; callers reuse a persistent connection where timing matters
  * bounded connect retries (never retries a *send* of a state-changing payload)
  * monotonic-clock timeouts and elapsed-time measurement
  * deterministic cleanup via context-manager support
  * no aggressive scanning, no uncontrolled concurrency
"""
from __future__ import annotations

import socket
import time


class TransportError(Exception):
    """Raised for connect/read/write failures with a clear, human message."""


class TcpClient:
    """A single persistent TCP connection. Reusable across operations (for timing sequences)."""

    def __init__(self, host: str, port: int, connect_timeout: float = 5.0,
                 read_timeout: float = 5.0, retries: int = 1):
        self.host = host
        self.port = int(port)
        self.connect_timeout = float(connect_timeout)
        self.read_timeout = float(read_timeout)
        self.retries = max(0, int(retries))
        self.sock: socket.socket | None = None

    # ---- lifecycle ----
    def connect(self) -> "TcpClient":
        if self.sock is not None:
            return self
        last = None
        for attempt in range(self.retries + 1):
            try:
                self.sock = socket.create_connection((self.host, self.port), self.connect_timeout)
                self.sock.settimeout(self.read_timeout)
                return self
            except OSError as e:
                last = e
                if attempt < self.retries:
                    time.sleep(min(0.25 * (attempt + 1), 1.0))
        raise TransportError(f"cannot connect to {self.host}:{self.port}: {last}")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self) -> "TcpClient":
        return self.connect()

    def __exit__(self, *exc) -> None:
        self.close()

    # ---- io ----
    def send(self, data: bytes) -> None:
        if self.sock is None:
            self.connect()
        try:
            self.sock.sendall(data)
        except OSError as e:
            raise TransportError(f"send failed: {e}") from e

    def recv_some(self, bufsize: int = 4096) -> bytes:
        """One recv(); b'' on orderly close. Raises TransportError only on a hard error."""
        if self.sock is None:
            raise TransportError("not connected")
        try:
            return self.sock.recv(bufsize)
        except socket.timeout:
            return b""
        except OSError as e:
            raise TransportError(f"recv failed: {e}") from e

    def recv_exact(self, n: int) -> bytes:
        """Read exactly n bytes (for length-prefixed frames). Returns fewer only on close/timeout."""
        buf = b""
        while len(buf) < n:
            chunk = self.recv_some(n - len(buf))
            if not chunk:
                break
            buf += chunk
        return buf

    def drain(self, idle: float = 0.6, cap: int = 8192) -> bytes:
        """Accumulate stream bytes until a short idle gap or cap (for multi-frame replies)."""
        if self.sock is None:
            raise TransportError("not connected")
        prev = self.sock.gettimeout()
        self.sock.settimeout(idle)
        buf = b""
        try:
            while len(buf) < cap:
                try:
                    chunk = self.sock.recv(4096)
                except socket.timeout:
                    break
                except OSError as e:
                    raise TransportError(f"recv failed: {e}") from e
                if not chunk:
                    break
                buf += chunk
        finally:
            if self.sock is not None:
                self.sock.settimeout(prev)
        return buf


class UdpClient:
    """A UDP socket for the GOOSE training stream (passive receive and explicit send)."""

    def __init__(self, host: str, port: int, read_timeout: float = 4.0):
        self.host = host
        self.port = int(port)
        self.read_timeout = float(read_timeout)
        self.sock: socket.socket | None = None
        self._bound = False

    def open_rx(self) -> "UdpClient":
        """Bind for passive listening. Does NOT transmit anything."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", self.port))
        self.sock.settimeout(self.read_timeout)
        self._bound = True
        return self

    def open_tx(self) -> "UdpClient":
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(self.read_timeout)
        return self

    def recv(self, bufsize: int = 256):
        if self.sock is None:
            raise TransportError("socket not open")
        try:
            return self.sock.recvfrom(bufsize)
        except socket.timeout:
            return None
        except OSError as e:
            raise TransportError(f"recv failed: {e}") from e

    def sendto(self, data: bytes) -> None:
        if self.sock is None:
            self.open_tx()
        try:
            self.sock.sendto(data, (self.host, self.port))
        except OSError as e:
            raise TransportError(f"send failed: {e}") from e

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self) -> "UdpClient":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def tcp_probe(host: str, port: int, timeout: float = 2.0) -> tuple[bool, float]:
    """A single bounded connect attempt for `status`. Returns (reachable, elapsed_ms)."""
    t0 = time.monotonic()
    try:
        s = socket.create_connection((host, port), timeout)
        s.close()
        return True, (time.monotonic() - t0) * 1000.0
    except OSError:
        return False, (time.monotonic() - t0) * 1000.0
