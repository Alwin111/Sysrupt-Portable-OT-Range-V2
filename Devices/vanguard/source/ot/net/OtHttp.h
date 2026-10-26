// SPDX-License-Identifier: Apache-2.0
#pragma once

// The intentional web HMI front door for the OT range (port 80), matching the designed challenge
// entry: an operator login, a robots.txt that leaks the control path, and an admin control panel
// reachable by a broken function-level authorization check (the privilege escalation). This is the
// range's own HTTP server -- Tactility's filesystem web server stays disabled.
//
// It is a live *view* onto the reactor (named sensor/actuator gauges, polled as JSON) so a player
// can watch values move while they probe the Modbus register map; the actual control channel is
// still Modbus/TCP on 502.

struct httpd_req; // from esp_http_server.h (httpd_req_t)

namespace ot::net {

// True if the request carries a valid engineer session cookie (set at /login). Exposed so the
// Defensive intranet (DefWeb) can gate its routes behind the same engineer login as the reactor HMI:
// only /robots.txt and /control (which discloses the credentials) are reachable unauthenticated.
bool engineerSessionValid(httpd_req* req);

// Mark the response non-persistent (Connection: close) so the embedded httpd frees the socket + its
// request buffer as soon as the response is sent. On this heap-constrained device the socket pool is
// capped at 4; without this a browser's idle keep-alive connections tie those slots up (and ~2 KB of
// header buffer each), which starves new page loads under concurrency. Called by the page/API handlers
// here and in DefWeb.
void setNoKeepAlive(httpd_req* req);

class OtHttp {
public:
    bool start();
    void stop();
    bool running() const { return server_ != nullptr; }

private:
    void* server_ = nullptr; // httpd_handle_t
};

} // namespace ot::net
