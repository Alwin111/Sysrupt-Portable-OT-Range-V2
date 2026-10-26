// SPDX-License-Identifier: Apache-2.0
#include "Modbus.h"

namespace ot::modbus {

namespace {

enum FunctionCode : uint8_t {
    FC_READ_HOLDING = 0x03,
    FC_READ_INPUT = 0x04,
    FC_WRITE_SINGLE = 0x06,
    FC_WRITE_MULTIPLE = 0x10,
};

enum Exception : uint8_t {
    EX_ILLEGAL_FUNCTION = 0x01,
    EX_ILLEGAL_DATA_ADDRESS = 0x02,
    EX_ILLEGAL_DATA_VALUE = 0x03,
};

uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); }

// Builds a response whose PDU has already been written into resp starting at MBAP_LEN, pduLen long.
// Copies the transaction id + unit id from the request and fixes up the MBAP length field.
size_t finish(const uint8_t* req, uint8_t* resp, size_t pduLen) {
    resp[0] = req[0]; // transaction id high
    resp[1] = req[1]; // transaction id low
    resp[2] = 0;      // protocol id = 0
    resp[3] = 0;
    wr16(&resp[4], (uint16_t)(pduLen + 1)); // length = unit id + PDU
    resp[6] = req[6];                       // unit id echoed
    return MBAP_LEN + pduLen;
}

size_t exception(const uint8_t* req, uint8_t* resp, uint8_t fc, uint8_t code) {
    resp[MBAP_LEN] = (uint8_t)(fc | 0x80);
    resp[MBAP_LEN + 1] = code;
    return finish(req, resp, 2);
}

} // namespace

size_t process(const uint8_t* req, size_t reqLen, uint8_t* resp, size_t respCap, Handler& handler) {
    if (reqLen < MBAP_LEN + 1 || respCap < MAX_ADU) {
        return 0;
    }
    // Protocol id must be 0; the length field must agree with what we actually have.
    if (rd16(&req[2]) != 0) {
        return 0;
    }
    const uint16_t mbapLen = rd16(&req[4]);
    if (mbapLen < 2 || (size_t)mbapLen + 6 > reqLen) {
        return 0; // wait for the rest of the ADU
    }

    const uint8_t fc = req[MBAP_LEN];
    const uint8_t* data = &req[MBAP_LEN + 1];
    const size_t dataLen = (size_t)mbapLen - 2; // bytes after the function code

    switch (fc) {
        case FC_READ_HOLDING:
        case FC_READ_INPUT: {
            if (dataLen < 4) return exception(req, resp, fc, EX_ILLEGAL_DATA_VALUE);
            const uint16_t start = rd16(&data[0]);
            const uint16_t count = rd16(&data[2]);
            if (count < 1 || count > MAX_READ_REGS) return exception(req, resp, fc, EX_ILLEGAL_DATA_VALUE);
            const uint8_t byteCount = (uint8_t)(count * 2);
            resp[MBAP_LEN] = fc;
            resp[MBAP_LEN + 1] = byteCount;
            for (uint16_t i = 0; i < count; i++) {
                const uint16_t value = fc == FC_READ_HOLDING
                    ? handler.readHolding((uint16_t)(start + i))
                    : handler.readInput((uint16_t)(start + i));
                wr16(&resp[MBAP_LEN + 2 + i * 2], value);
            }
            return finish(req, resp, (size_t)2 + byteCount);
        }

        case FC_WRITE_SINGLE: {
            if (dataLen < 4) return exception(req, resp, fc, EX_ILLEGAL_DATA_VALUE);
            const uint16_t addr = rd16(&data[0]);
            const uint16_t value = rd16(&data[2]);
            if (!handler.writeHolding(addr, value)) {
                return exception(req, resp, fc, EX_ILLEGAL_DATA_ADDRESS);
            }
            // Echo the request's PDU back verbatim.
            resp[MBAP_LEN] = fc;
            wr16(&resp[MBAP_LEN + 1], addr);
            wr16(&resp[MBAP_LEN + 3], value);
            return finish(req, resp, 5);
        }

        case FC_WRITE_MULTIPLE: {
            if (dataLen < 5) return exception(req, resp, fc, EX_ILLEGAL_DATA_VALUE);
            const uint16_t start = rd16(&data[0]);
            const uint16_t count = rd16(&data[2]);
            const uint8_t byteCount = data[4];
            if (count < 1 || count > MAX_WRITE_REGS || byteCount != count * 2 || dataLen < (size_t)5 + byteCount) {
                return exception(req, resp, fc, EX_ILLEGAL_DATA_VALUE);
            }
            // Applied in order; a read-only/out-of-range address aborts with an exception, leaving
            // the registers before it written (as many real devices do). The handler's own policy
            // decides which addresses are writable.
            for (uint16_t i = 0; i < count; i++) {
                const uint16_t addr = (uint16_t)(start + i);
                const uint16_t value = rd16(&data[5 + i * 2]);
                if (!handler.writeHolding(addr, value)) {
                    return exception(req, resp, fc, EX_ILLEGAL_DATA_ADDRESS);
                }
            }
            resp[MBAP_LEN] = fc;
            wr16(&resp[MBAP_LEN + 1], start);
            wr16(&resp[MBAP_LEN + 3], count);
            return finish(req, resp, 5);
        }

        default:
            return exception(req, resp, fc, EX_ILLEGAL_FUNCTION);
    }
}

} // namespace ot::modbus
