# SPDX-License-Identifier: Apache-2.0
"""The execution context shared by every command handler.

It centralises target/port resolution, transport creation and the connection lifecycle. In normal
single-command mode, connections opened via ``tcp()`` are closed right after the command. Under the
timing/sequence engine (``keepalive=True``) they are cached per protocol and reused across steps --
this is what lets a Modbus unlock+write race run down one persistent connection -- then closed when
the sequence ends.
"""
from __future__ import annotations

from .config import Config
from .history import History
from .transport import TcpClient, UdpClient


class Context:
    def __init__(self, cfg: Config, history: History | None = None, verbose: bool = False,
                 target_override: str | None = None):
        self.cfg = cfg
        self.history = history or History()
        self.verbose = verbose
        self._target = target_override or cfg.target
        self.keepalive = False
        self._cache: dict = {}
        self._transient: list = []

    @property
    def target(self) -> str:
        return self._target

    def set_target(self, t: str) -> None:
        self._target = t

    def port(self, proto: str) -> int:
        return self.cfg.port(proto)

    # ---- transport ----
    def _new(self, proto: str) -> TcpClient:
        return TcpClient(self._target, self.cfg.port(proto),
                         connect_timeout=self.cfg.connect_timeout,
                         read_timeout=self.cfg.read_timeout,
                         retries=self.cfg.retries).connect()

    def tcp(self, proto: str) -> TcpClient:
        """A TCP client; reused across steps when keepalive is on, else closed after the command."""
        if self.keepalive:
            c = self._cache.get(proto)
            if c is None or c.sock is None:
                c = self._new(proto)
                self._cache[proto] = c
            return c
        c = self._new(proto)
        self._transient.append(c)
        return c

    def new_tcp(self, proto: str) -> TcpClient:
        """Always a fresh connection (for protocols that serve one request per connection)."""
        c = self._new(proto)
        self._transient.append(c)
        return c

    def udp(self, proto: str) -> UdpClient:
        return UdpClient(self._target, self.cfg.port(proto), read_timeout=self.cfg.read_timeout)

    def close_transient(self) -> None:
        for c in self._transient:
            c.close()
        self._transient.clear()

    def close_all(self) -> None:
        self.close_transient()
        for c in self._cache.values():
            c.close()
        self._cache.clear()
