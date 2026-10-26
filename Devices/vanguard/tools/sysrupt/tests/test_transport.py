# SPDX-License-Identifier: Apache-2.0
import socket
import struct

import pytest

from sysrupt.transport import TcpClient, TransportError, UdpClient, tcp_probe


def _free_but_closed_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()           # now nothing listens here -> connect is refused
    return port


def test_connect_refused_raises_transport_error():
    port = _free_but_closed_port()
    c = TcpClient("127.0.0.1", port, connect_timeout=1.0, retries=0)
    with pytest.raises(TransportError):
        c.connect()


def test_tcp_probe_unreachable():
    port = _free_but_closed_port()
    ok, ms = tcp_probe("127.0.0.1", port, timeout=1.0)
    assert ok is False and ms >= 0


def test_recv_exact_and_drain_over_socketpair():
    a, b = socket.socketpair()
    try:
        client = TcpClient.__new__(TcpClient)       # bypass connect
        client.sock = a
        client.read_timeout = 1.0
        a.settimeout(1.0)
        b.sendall(b"\x01\x02\x03\x04\x05")
        assert client.recv_exact(3) == b"\x01\x02\x03"
        assert client.recv_exact(2) == b"\x04\x05"
        b.sendall(b"hello")
        b.close()
        assert client.drain(idle=0.3) == b"hello"
    finally:
        a.close()


def test_recv_exact_short_on_close():
    a, b = socket.socketpair()
    client = TcpClient.__new__(TcpClient)
    client.sock = a
    client.read_timeout = 1.0
    a.settimeout(1.0)
    b.sendall(b"\x01\x02")
    b.close()
    assert client.recv_exact(8) == b"\x01\x02"       # returns what arrived, no hang
    a.close()


def test_udp_send_does_not_raise_without_listener():
    u = UdpClient("127.0.0.1", _free_but_closed_port())
    u.open_tx()
    u.sendto(b"ping")      # UDP send to nowhere must not raise
    u.close()
