// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <esp_http_server.h>

// Registers the shared Defensive web surface on an already-started HTTP server (the range runs a
// single httpd on :80, owned by OtHttp). Routes:
//   /intranet    investigation hub (case brief + links)
//   /mail        seeded webmail inbox (?id=N for one message)
//   /historian   historian SQL query console; ?sql=<statement> returns a JSON result set (sim/HistSql)
//   /inventory   engineering asset inventory + OUI registry
//   /evidence    raw evidence fixtures (?f=dcp_capture|asset_inventory|historian_access_log|...)
//   /investigate submission forms for the three Defensive findings
//   /submit      POST: drives the DefService completion predicates, returns the flag on success
//
// The handlers are device-only (esp_http_server); the investigation logic + evidence they render
// live in sim/Defensive (host-tested) and the latching state in DefService.
namespace ot::net {

void registerDefRoutes(httpd_handle_t server);

} // namespace ot::net
