// SPDX-License-Identifier: Apache-2.0
#pragma once

// The process historian console as a SEPARATE web property (HIST-04), on its OWN port (:8443) with
// its OWN look and its OWN access gate - distinct from the corporate portal on :80. It is discovered
// (not linked) via the corporate portal's mail threads (soc@/eng@ reference 10.0.1.1:8443). This is a
// second esp_http_server instance; on this no-PSRAM board it is kept deliberately small (few sockets).
//
// It serves the read-only SQL-query interface (SHOW TABLES / DESCRIBE / SELECT over sim/HistSql, the
// same view onto ot::hist::Historian the :9000 protocol uses) PLUS a deliberately injectable "tag
// search" field whose backend concatenates user input into the query (sim/HistSql::searchTag), so a
// classic SQL injection can reach the purged "incident window" rows the console otherwise hides.
//
// On this no-PSRAM board a second esp_http_server instance (originally :8443) left free heap at ~5 KB
// and destabilised the Wi-Fi AP, so the console is served as routes on the single :80 server instead:
//   /hist          historian login / SQL console (own look + own vghist login)
//   /hist/login    POST login (historian/readonly)
//   /hist/query    JSON query API (?sql=... safe engine ; ?search=... injectable tag search)
//   /hist-config   the exposed service config that leaks the weak credential (OSINT discovery)

#include <esp_http_server.h>

namespace ot::net {

// Registers the historian console routes on the given (already-started) :80 server handle.
void registerHistorianRoutes(httpd_handle_t server);

} // namespace ot::net
