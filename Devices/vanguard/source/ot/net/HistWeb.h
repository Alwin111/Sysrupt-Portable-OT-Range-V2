// SPDX-License-Identifier: Apache-2.0
#pragma once

// The process historian console as a SEPARATE internal system (HIST-04) with its OWN look and its OWN
// access gate (login historian/readonly) - distinct from the corporate portal. It is discovered (not
// linked in the plant nav) via the corporate portal's mail threads (soc@/eng@ reference 10.0.1.1/hist).
//
// It exposes a single "Filter by Source" data interface whose backend concatenates user input straight
// into its query (sim/HistSqlUnion), so a classic UNION-based SQL injection extracts the hidden flag
// from the flags table - the #07 Historian SQL Injection challenge. The extracted flag is then validated
// when the player submits it on the corporate portal (ChKind::Forensics).
//
// On this no-PSRAM board a second esp_http_server instance left free heap at ~5 KB and destabilised the
// Wi-Fi AP, so the console is served as routes on the single :80 server instead:
//   /hist          historian login / Filter-by-Source console (own look + own vghist login)
//   /hist/login    POST login (historian/readonly)
//   /hist/analyze  JSON query API (?source=... injectable Filter-by-Source)
//   /hist-config   the exposed service config that leaks the weak credential (OSINT discovery)

#include <esp_http_server.h>

namespace ot::net {

// Registers the historian console routes on the given (already-started) :80 server handle.
void registerHistorianRoutes(httpd_handle_t server);

} // namespace ot::net
