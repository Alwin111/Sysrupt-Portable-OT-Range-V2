// SPDX-License-Identifier: Apache-2.0
// Integrity test for the embedded /toolkit.zip asset (source/ot/net/ToolkitZip.cpp): it must be
// byte-for-byte identical to the actual tested release archive, so the firmware serves exactly what
// was tested. Run from test/:
//   g++ -std=c++17 -I../source/ot ot_toolkitzip_tests.cpp ../source/ot/net/ToolkitZip.cpp && ./a.out
#include "net/ToolkitZip.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int g_pass = 0, g_fail = 0;
void fail(const char* f, int l, const char* e) { fprintf(stderr, "FAIL %s:%d: %s\n", f, l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__FILE__, __LINE__, #expr); } while (0)

static const char* RELEASE = "../tools/sysrupt/release/sysrupt-toolkit-1.0.0.zip";

static std::vector<unsigned char> readFile(const char* path) {
    std::vector<unsigned char> out;
    FILE* f = std::fopen(path, "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize((size_t)n);
        size_t got = std::fread(out.data(), 1, (size_t)n, f);
        out.resize(got);
    }
    std::fclose(f);
    return out;
}

int main() {
    // The embedded blob looks like a ZIP (PK\x03\x04) and is non-trivial.
    CHECK(toolkit_zip_len > 1000);
    CHECK(toolkit_zip[0] == 'P' && toolkit_zip[1] == 'K');

    // The recorded SHA-256 is a 64-char hex string.
    CHECK(std::strlen(toolkit_zip_sha256) == 64);
    bool hex = true;
    for (int i = 0; i < 64; i++) {
        char c = toolkit_zip_sha256[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) hex = false;
    }
    CHECK(hex);
    CHECK(std::string(toolkit_zip_name).find(".zip") != std::string::npos);

    // The embedded bytes must equal the actual release archive exactly.
    std::vector<unsigned char> file = readFile(RELEASE);
    if (file.empty()) {
        fprintf(stderr, "NOTE: release archive not found at %s -- regenerate it and the asset.\n", RELEASE);
        CHECK(false);
    } else {
        CHECK(file.size() == toolkit_zip_len);
        if (file.size() == toolkit_zip_len) {
            CHECK(std::memcmp(file.data(), toolkit_zip, toolkit_zip_len) == 0);
        }
    }

    printf("toolkitzip tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
