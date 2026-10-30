# SPDX-License-Identifier: Apache-2.0
"""Mocked transports and frame builders for the automated tests. No real sockets are used."""
import struct
import types


# ---- fake TCP (serves a preset byte stream via a cursor) ----
class FakeTcp:
    def __init__(self, response: bytes = b""):
        self.response = response
        self.cursor = 0
        self.sent = b""
        self.closed = False
        self.sock = object()

    def send(self, data: bytes):
        self.sent += data

    def connect(self):
        return self

    def recv_exact(self, n: int) -> bytes:
        chunk = self.response[self.cursor:self.cursor + n]
        self.cursor += len(chunk)
        return chunk

    def recv_some(self, n: int = 4096) -> bytes:
        return self.recv_exact(n)

    def drain(self, idle: float = 0.6, cap: int = 8192) -> bytes:
        chunk = self.response[self.cursor:]
        self.cursor = len(self.response)
        return chunk

    def close(self):
        self.closed = True


# ---- fake UDP ----
class FakeUdp:
    def __init__(self, rx_frames=None, reply: bytes = None, raise_after=False):
        self.rx = list(rx_frames or [])
        self.reply = reply
        self.raise_after = raise_after      # raise KeyboardInterrupt once rx is drained (ends sniff)
        self.sent = []
        self.closed = False
        self.sock = types.SimpleNamespace(settimeout=lambda t: None)

    def open_rx(self):
        return self

    def open_tx(self):
        return self

    def recv(self, n: int = 256):
        if self.sent and self.reply is not None:
            r, self.reply = self.reply, None
            return (r, ("127.0.0.1", 0))
        if self.rx:
            return (self.rx.pop(0), ("127.0.0.1", 0))
        if self.raise_after:
            raise KeyboardInterrupt
        return None

    def sendto(self, data: bytes):
        self.sent.append(data)

    def close(self):
        self.closed = True


# ---- fake execution context ----
class FakeCtx:
    """Implements the slice of Context the handlers use. TCP responses are a queue of byte frames."""
    def __init__(self, target="10.0.1.1", tcp_responses=None, udp=None, ports=None):
        self._target = target
        self.verbose = False
        self.keepalive = False
        self._tcp_responses = list(tcp_responses or [])
        self._udp = udp
        self.conns = []            # every FakeTcp handed out, for assertions
        self.udp_conns = []
        self._ports = ports or {"modbus": 502, "goose": 6180, "historian": 9000, "web": 80}

    @property
    def target(self):
        return self._target

    def port(self, proto):
        return self._ports[proto]

    def _next_tcp(self):
        resp = self._tcp_responses.pop(0) if self._tcp_responses else b""
        c = FakeTcp(resp)
        self.conns.append(c)
        return c

    def tcp(self, proto):
        return self._next_tcp()

    def new_tcp(self, proto):
        return self._next_tcp()

    def udp(self, proto):
        u = self._udp if self._udp is not None else FakeUdp()
        self.udp_conns.append(u)
        return u

    def close_transient(self):
        for c in self.conns:
            c.close()

    def close_all(self):
        self.close_transient()


# ---- Modbus frame builders ----
def mb_read_resp(regs, fc=3):
    pdu = bytes([fc, len(regs) * 2]) + b"".join(struct.pack(">H", r) for r in regs)
    return struct.pack(">HHHB", 1, 0, len(pdu) + 1, 1) + pdu


def mb_exc(fc, code):
    pdu = bytes([fc | 0x80, code])
    return struct.pack(">HHHB", 1, 0, len(pdu) + 1, 1) + pdu


def mb_write_echo(addr, val):
    pdu = struct.pack(">BHH", 6, addr, val)
    return struct.pack(">HHHB", 1, 0, len(pdu) + 1, 1) + pdu


# ---- IEC-104 frame builders (server side; cot is NOT masked so P/N can be set) ----
STARTDT_CON = bytes([0x68, 0x04, 0x0B, 0, 0, 0])


def srv_i(typ, cot, org, ca, ioa, ie):
    b = bytearray([0x68, 0, 0, 0, 0, 0,
                   typ, 0x01, cot & 0xFF, org, ca & 0xFF, (ca >> 8) & 0xFF,
                   ioa & 0xFF, (ioa >> 8) & 0xFF, (ioa >> 16) & 0xFF])
    b += bytes(ie)
    b[1] = len(b) - 2
    return bytes(b)


def iec_flag_frames(flag: str, base=5000):
    """Encode a flag string into M_ME (type 9) objects the way the sim exposes it."""
    data = flag.encode() + b"\x00" * (32 - len(flag))
    out = b""
    for i in range(16):
        hi, lo = data[i * 2], data[i * 2 + 1]
        word = (hi << 8) | lo
        out += srv_i(9, 20, 0, 1, base + i, struct.pack("<H", word))
    return out


def iec_interro(point, value):
    """STARTDT_CON + one single-point (type 1) I-frame carrying `value` (0/1) at IOA `point`.

    Shaped for a wait-until poll: _startdt_on consumes the 6-byte STARTDT_CON, then the interrogation
    drain() returns the I-frame(s).
    """
    return STARTDT_CON + srv_i(1, 20, 0, 1, point, bytes([value & 0x01]))


# ---- TriStation frame builders ----
TS_SYNC = bytes([0xAA, 0x55])


def ts_read_resp(reg, val):
    """A 7-byte TriStation READ reply: AA 55 01 rH rL vH vL."""
    return TS_SYNC + bytes([0x01, (reg >> 8) & 0xFF, reg & 0xFF, (val >> 8) & 0xFF, val & 0xFF])


class FailingUdp(FakeUdp):
    """A UDP fake whose sendto always raises TransportError (for send-failure tests)."""
    def sendto(self, data):
        from sysrupt.transport import TransportError
        raise TransportError("send failed (test)")
