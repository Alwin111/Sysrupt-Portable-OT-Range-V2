// SPDX-License-Identifier: Apache-2.0
// Host tests for the challenge catalogue + difficulty-based scoring (the authoritative definition in
// Challenges.h). Verifies the 11 active challenges, unique IDs, Ch05 absent, Ch08/Ch10 Defensive &
// distinct, and that the attainable total is exactly 2500 (= SCORE_MAX, derived from the catalogue).
// Run: g++ -std=c++17 -I../source/ui ot_catalogue_tests.cpp && ./a.out
#include "Challenges.h"

#include <cstdint>
#include <cstdio>
#include <set>

using namespace ot::ui;

namespace {
int g_pass = 0, g_fail = 0;
void fail(int l, const char* e) { fprintf(stderr, "FAIL %d: %s\n", l, e); g_fail++; }
}
#define CHECK(expr) do { if (expr) g_pass++; else fail(__LINE__, #expr); } while (0)

static const ChallengeDef* find(int num) {
    for (const auto& c : CHALLENGES) if (c.number == num) return &c;
    for (const auto& c : DEFENSIVE) if (c.number == num) return &c;
    return nullptr;
}

int main() {
    // counts
    CHECK(CHALLENGE_COUNT == 7);           // offensive
    CHECK(DEFENSIVE_COUNT == 4);           // defensive
    CHECK(CHALLENGE_COUNT + DEFENSIVE_COUNT == 11);

    // total attainable = exactly 2500 and SCORE_MAX derives from the catalogue
    int sum = 0;
    for (const auto& c : CHALLENGES) sum += c.points;
    for (const auto& c : DEFENSIVE) sum += c.points;
    CHECK(sum == 2500);
    CHECK(SCORE_MAX == 2500);
    CHECK(SCORE_MAX == sum);               // the max is derived, not hard-coded

    // unique IDs; every challenge has a positive, difficulty-tier score; both categories populated
    std::set<int> ids;
    int offensive = 0, defensive = 0;
    auto scan = [&](const ChallengeDef& c) {
        ids.insert(c.number);
        CHECK(c.points > 0);
        CHECK(c.points == 100 || c.points == 150 || c.points == 200 || c.points == 250 ||
              c.points == 300 || c.points == 350); // difficulty tiers (Easy 100 / Medium 150 / Hard 200-250 / Expert 300)
        if (c.category == Category::Offensive) offensive++; else defensive++;
    };
    for (const auto& c : CHALLENGES) scan(c);
    for (const auto& c : DEFENSIVE) scan(c);
    CHECK((int)ids.size() == 11);          // all IDs unique
    CHECK(offensive == 7 && defensive == 4);

    // Challenge 05 (Dial-In Dam) must be absent from the live catalogue
    CHECK(find(5) == nullptr);

    // Challenge 08 present, Defensive, SNMP recovery backend
    const ChallengeDef* c8 = find(8);
    CHECK(c8 != nullptr);
    CHECK(c8 && c8->category == Category::Defensive);
    CHECK(c8 && c8->kind == ChKind::SnmpRecovery);

    // Challenge 10 present, Defensive, DCP identity (independent of #08's SNMP kind)
    const ChallengeDef* c10 = find(10);
    CHECK(c10 != nullptr);
    CHECK(c10 && c10->category == Category::Defensive);
    CHECK(c10 && c10->kind == ChKind::Device);
    CHECK(c8 && c10 && c8->kind != c10->kind);   // distinct challenges, not merged

    // Expected difficulty-based allocation (documents the intended per-challenge value)
    struct Exp { int num; int pts; };
    const Exp expected[] = { {1,250},{2,250},{3,150},{4,200},{6,300},{11,300},{12,300},
                             {7,250},{8,250},{9,150},{10,100} };
    for (const auto& e : expected) {
        const ChallengeDef* c = find(e.num);
        CHECK(c != nullptr && c->points == e.pts);
    }

    // Score recalculation from a completion bitmask (how the firmware restores score after a reboot:
    // it never stores a numeric score, only the completion bits, and re-sums the live catalogue).
    auto scoreForMask = [](uint32_t mask) {
        int s = 0;
        for (const auto& c : CHALLENGES) if (mask & (1u << c.number)) s += c.points;
        for (const auto& c : DEFENSIVE) if (mask & (1u << c.number)) s += c.points;
        return s;
    };
    CHECK(scoreForMask(0) == 0);                                   // fresh install / after reset => 0
    CHECK(scoreForMask(1u << 6) == 300);                           // GOOSE only
    CHECK(scoreForMask((1u << 6) | (1u << 8)) == 550);            // GOOSE (300) + SNMP recovery (250)
    CHECK(scoreForMask(0xFFFFFFFFu) == 2500);                      // everything completed => the max
    CHECK(scoreForMask(1u << 5) == 0);                            // a stale removed-#05 bit scores nothing
    // Idempotence: a mask is a set of bits, so "completing twice" (setting the same bit) is a no-op.
    uint32_t m = 1u << 11;
    m |= (1u << 11);
    CHECK(scoreForMask(m) == 300);                                // reactor counted once, not twice

    printf("catalogue tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
