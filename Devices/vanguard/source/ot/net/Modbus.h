// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// A small Modbus/TCP server codec: parse one request ADU (MBAP header + PDU) and build the
// response. Pure logic, no sockets -- so it is host-tested (test/ot_modbus_tests.cpp) and the
// socket task (ModbusServer) only has to move bytes.
//
// Supported function codes: 0x03 Read Holding Registers, 0x04 Read Input Registers,
// 0x06 Write Single Register, 0x10 Write Multiple Registers. Anything else returns the
// "illegal function" exception, as a real device would.
namespace ot::modbus {

// Where register reads/writes are serviced. The implementation (OtService) serializes these under
// the plant mutex; the codec itself never blocks.
struct Handler {
    virtual ~Handler() = default;
    virtual uint16_t readHolding(uint16_t addr) = 0;
    virtual uint16_t readInput(uint16_t addr) = 0;
    // Returns false for a read-only or out-of-range address, which becomes an "illegal data
    // address" exception.
    virtual bool writeHolding(uint16_t addr, uint16_t value) = 0;
};

constexpr size_t MBAP_LEN = 7;       // transaction(2) protocol(2) length(2) unit(1)
constexpr size_t MAX_ADU = 260;      // Modbus/TCP maximum
constexpr uint16_t MAX_READ_REGS = 125;
constexpr uint16_t MAX_WRITE_REGS = 123;

// Processes one request ADU and writes the response ADU into resp (capacity respCap).
// Returns the response length, or 0 if the request was too short/malformed to even reply to
// (the caller should then wait for more bytes or close).
size_t process(const uint8_t* req, size_t reqLen, uint8_t* resp, size_t respCap, Handler& handler);

} // namespace ot::modbus
