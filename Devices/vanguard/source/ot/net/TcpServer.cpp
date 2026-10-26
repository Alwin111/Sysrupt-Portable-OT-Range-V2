// SPDX-License-Identifier: Apache-2.0
#include "TcpServer.h"

#include <tactility/log.h>

#include <lwip/sockets.h>

#include <cerrno>
#include <cstring>

namespace ot::net {

namespace {

constexpr auto* TAG = "OtTcp";
constexpr int SELECT_TIMEOUT_MS = 200; // so *running is re-checked promptly
constexpr size_t RX_CHUNK = 512;

int makeListenSocket(uint16_t port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        LOG_E(TAG, "socket() failed: %d", errno);
        return -1;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        LOG_E(TAG, "bind(:%u) failed: %d", (unsigned)port, errno);
        close(fd);
        return -1;
    }
    if (listen(fd, backlog) != 0) {
        LOG_E(TAG, "listen(:%u) failed: %d", (unsigned)port, errno);
        close(fd);
        return -1;
    }
    return fd;
}

} // namespace

void serve(uint16_t port, int maxClients, ConnectionHandler& handler, const std::atomic<bool>& running) {
    int listenFd = makeListenSocket(port, maxClients);
    if (listenFd < 0) {
        return;
    }
    LOG_I(TAG, "Listening on :%u", (unsigned)port);

    struct Client {
        int fd = -1;
        void* ctx = nullptr;
    };
    Client* clients = new Client[maxClients];

    uint8_t chunk[RX_CHUNK];

    while (running.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listenFd, &rfds);
        int maxFd = listenFd;
        for (int i = 0; i < maxClients; i++) {
            if (clients[i].fd >= 0) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > maxFd) maxFd = clients[i].fd;
            }
        }

        timeval tv = { .tv_sec = 0, .tv_usec = SELECT_TIMEOUT_MS * 1000 };
        int ready = select(maxFd + 1, &rfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            LOG_E(TAG, "select() failed: %d", errno);
            break;
        }
        if (ready == 0) {
            continue; // timeout: loop to re-check running
        }

        // New connection
        if (FD_ISSET(listenFd, &rfds)) {
            int fd = accept(listenFd, nullptr, nullptr);
            if (fd >= 0) {
                // Enable TCP keepalive so the stack reaps half-open peers (a player's phone that
                // power-saves/roams drops Wi-Fi without a FIN, leaving a dead connection that would
                // otherwise hold a scarce client slot for minutes). Reaped in ~30s + 3x10s.
                int one = 1, idle = 30, intvl = 10, cnt = 3;
                setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
                setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
                setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
                setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
                int slot = -1;
                for (int i = 0; i < maxClients; i++) {
                    if (clients[i].fd < 0) { slot = i; break; }
                }
                if (slot < 0) {
                    close(fd); // full: refuse
                } else {
                    clients[slot].fd = fd;
                    clients[slot].ctx = handler.onOpen();
                }
            }
        }

        // Existing connections
        for (int i = 0; i < maxClients; i++) {
            if (clients[i].fd < 0 || !FD_ISSET(clients[i].fd, &rfds)) {
                continue;
            }
            int n = recv(clients[i].fd, chunk, sizeof(chunk), 0);
            bool keep = n > 0 && handler.onData(clients[i].fd, clients[i].ctx, chunk, (size_t)n);
            if (!keep) {
                handler.onClose(clients[i].ctx);
                close(clients[i].fd);
                clients[i].fd = -1;
                clients[i].ctx = nullptr;
            }
        }
    }

    for (int i = 0; i < maxClients; i++) {
        if (clients[i].fd >= 0) {
            handler.onClose(clients[i].ctx);
            close(clients[i].fd);
        }
    }
    delete[] clients;
    close(listenFd);
    LOG_I(TAG, "Stopped :%u", (unsigned)port);
}

} // namespace ot::net
