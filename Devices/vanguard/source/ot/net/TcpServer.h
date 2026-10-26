// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// A small select()-based TCP server: one FreeRTOS task serves up to maxClients connections on one
// port. Framing is the handler's job -- it is handed raw received bytes and the socket fd, buffers
// and replies as it sees fit. Used for both Modbus (:502) and S7comm (:102), which share the one
// plant behind the handler.
namespace ot::net {

struct ConnectionHandler {
    virtual ~ConnectionHandler() = default;
    // Allocate per-connection state (e.g. a receive buffer / session), or return nullptr.
    virtual void* onOpen() { return nullptr; }
    virtual void onClose(void* ctx) { (void)ctx; }
    // Called with each chunk received. Return false to close the connection. Reply by writing to
    // fd (the handler owns send()).
    virtual bool onData(int fd, void* ctx, const uint8_t* data, size_t len) = 0;
};

// Runs the accept/select loop until *running becomes false. Blocks (call it as a task body).
void serve(uint16_t port, int maxClients, ConnectionHandler& handler, const std::atomic<bool>& running);

} // namespace ot::net
