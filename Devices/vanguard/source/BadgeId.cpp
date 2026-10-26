// SPDX-License-Identifier: Apache-2.0
#include "BadgeId.h"

#include <esp_mac.h>
#include <nvs.h>

namespace badge {

namespace {

constexpr const char* NVS_NS = "vgbadge"; // dedicated namespace (not the session store "vgsession")
constexpr const char* NVS_KEY = "no";     // u16 badge number, 1..999

char g_ssid[33] = {};
char g_label[24] = {};
int g_number = -1;
bool g_init = false;

// Computed once. First use is on the LVGL task while the starting screen is built (before the AP /
// service tasks are spawned), so this is effectively single-threaded; a later concurrent first call
// would at worst recompute the identical result into the same buffers.
void init() {
    if (g_init) {
        return;
    }

    int num = -1;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint16_t n = 0;
        if (nvs_get_u16(h, NVS_KEY, &n) == ESP_OK && n >= 1 && n <= 999) {
            num = (int)n;
        }
        nvs_close(h);
    }

    unsigned char mac[6] = {};
    esp_efuse_mac_get_default(mac); // factory MAC; stable for the life of the device

    format(num, mac, g_ssid, g_label);
    g_number = num;
    g_init = true;
}

} // namespace

const char* ssid() { init(); return g_ssid; }
const char* label() { init(); return g_label; }
bool configured() { init(); return g_number > 0; }
int number() { init(); return g_number; }

} // namespace badge
