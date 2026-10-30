// SPDX-License-Identifier: Apache-2.0
#include "Challenges.h"
#include "SessionCodec.h"

#include "../ot/Iec104Service.h"
#include "../ot/OpcUaService.h"
#include "../ot/OtService.h"
#include "../ot/PumpService.h"
#include "../ot/SisService.h"
#include "../ot/DamService.h"
#include "../ot/DefService.h"
#include "../ot/GooseService.h"
#include "../ot/SnmpService.h"

#include <tactility/log.h>

#include <nvs.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstdio>

namespace ot::ui {

namespace {
int g_selected = 1; // first challenge on the board (defensive-first numbering: 01 Historian SQLi)
}

void setSelectedChallenge(int number) { g_selected = number; }
int selectedChallenge() { return g_selected; }

const ChallengeDef& challengeByNumber(int number) {
    for (const auto& c : CHALLENGES) {
        if (c.number == number) {
            return c;
        }
    }
    for (const auto& c : DEFENSIVE) {
        if (c.number == number) {
            return c;
        }
    }
    return CHALLENGES[0];
}

int nextChallengeNumber(int currentNumber) {
    // Walk the array that contains currentNumber; return the next entry's number, or -1 if it is last.
    const ChallengeDef* arr = nullptr;
    int count = 0;
    for (int i = 0; i < CHALLENGE_COUNT; i++) {
        if (CHALLENGES[i].number == currentNumber) { arr = CHALLENGES; count = CHALLENGE_COUNT; break; }
    }
    if (arr == nullptr) {
        for (int i = 0; i < DEFENSIVE_COUNT; i++) {
            if (DEFENSIVE[i].number == currentNumber) { arr = DEFENSIVE; count = DEFENSIVE_COUNT; break; }
        }
    }
    if (arr == nullptr) return -1;
    for (int i = 0; i < count; i++) {
        if (arr[i].number == currentNumber) return (i + 1 < count) ? arr[i + 1].number : -1;
    }
    return -1;
}

const char* statusLabel(ChStatus s) {
    switch (s) {
        case ChStatus::NotStarted: return "NOT STARTED";
        case ChStatus::InProgress: return "IN PROGRESS";
        case ChStatus::Completed: return "COMPLETED";
        case ChStatus::NotImplemented: return "SOON";
    }
    return "";
}

const char* displayTitle(const ChallengeDef& c) {
    // Shorten only the long canonical titles for narrow contexts (list rows, breadcrumb, status).
    switch (c.number) {
        case 1:  return "HISTORIAN SQLi";   // 01 HISTORIAN SQL INJECTION
        case 7:  return "STABILISE REACTOR";// 07 STABILISE THE REACTOR
        case 8:  return "MAINT OVERRIDE";   // 08 MAINTENANCE OVERRIDE
        default: return c.name;             // OPC SCOUT, SECOND BLACKOUT, FIELD DEVICE RECOVERY,
                                            // TROJAN HISTORIAN, DEVICE TAKEOVER are already concise.
    }
}

const char* difficultyLabel(Difficulty d) {
    switch (d) {
        case Difficulty::Easy: return "EASY";
        case Difficulty::Medium: return "MED";
        case Difficulty::Hard: return "HARD";
        case Difficulty::Expert: return "EXPERT";
    }
    return "";
}

int earnedScore() {
    int sum = 0;
    for (const auto& c : CHALLENGES) {
        if (challengeStatus(c) == ChStatus::Completed) sum += c.points;
    }
    // Defensive completions count toward the score too (every challenge is worth its points); without
    // this a solved Defensive challenge would show "+N awarded" on the completion screen but never
    // move the total.
    for (const auto& c : DEFENSIVE) {
        if (challengeStatus(c) == ChStatus::Completed) sum += c.points;
    }
    return sum;
}

namespace {
struct Reveal { int num; int idx; };
Reveal g_reveals[32];
int g_revealCount = 0;
} // namespace

bool hintRevealed(int num, int idx) {
    for (int i = 0; i < g_revealCount; i++) {
        if (g_reveals[i].num == num && g_reveals[i].idx == idx) return true;
    }
    return false;
}

// ---- Persisted session state ---------------------------------------------------------------
// Challenge completion and hint usage are otherwise derived only from live service state in RAM, so
// a plain ESP32 reboot (which restarts the services fresh) would silently wipe the player's progress
// and score. We persist a compact snapshot (SessionCodec string) to ESP-IDF NVS and restore it on
// first access after boot. NVS is used (NOT a /data FAT file) specifically because nvs_commit()
// durably flushes the write to flash BEFORE returning, so progress survives the physical RESET
// button even immediately after a completion - a FAT file write (temp-file + rename) can sit
// unflushed in the filesystem cache and be lost on a sudden reset. The score itself is never stored;
// it is recalculated from this completion bitmask against the live catalogue. Cleared ONLY by the
// explicit Reset Session action - never by a reboot, app start/stop, or challenge (re)open.
namespace {
constexpr const char* NVS_NAMESPACE = "vgsession";  // dedicated namespace (not the Wi-Fi creds store)
constexpr const char* NVS_KEY = "state";
uint32_t g_persistedComplete = 0; // bit n set => challenge number n is completed (latched)
bool g_sessionLoaded = false;
bool g_sessionDirty = false;      // a completion was observed that is not yet committed to NVS
SemaphoreHandle_t g_sessionMutex = nullptr;

inline uint32_t sessionBit(int num) { return (num >= 0 && num < 32) ? (1u << (unsigned)num) : 0u; }

// Scratch kept at file scope (not on the stack): the codec string + decoded state. Every access is
// serialized by g_sessionMutex, so sharing one set of scratch buffers is safe.
char g_sessBuf[512];
SessionState g_sessState;

void sessionLock() {
    if (g_sessionMutex == nullptr) g_sessionMutex = xSemaphoreCreateMutex();
    if (g_sessionMutex) xSemaphoreTake(g_sessionMutex, portMAX_DELAY);
}
void sessionUnlock() {
    if (g_sessionMutex) xSemaphoreGive(g_sessionMutex);
}

// Reads the snapshot from disk into the in-RAM state. Missing/corrupt/incompatible data -> fresh.
// Returns false only if NVS was not ready yet (so the caller can retry later), not for an absent
// key (which is a legitimately fresh session).
bool sessionLoadFromDisk() {
    sessionLock();
    bool ok = false;
    nvs_handle_t h;
    const esp_err_t e = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (e == ESP_OK) {
        ok = true; // NVS is up; an absent key is a legitimately fresh first-run session
        size_t len = sizeof(g_sessBuf);
        if (nvs_get_str(h, NVS_KEY, g_sessBuf, &len) == ESP_OK &&
            sessionDeserialize(g_sessBuf, g_sessState)) {
            g_persistedComplete = g_sessState.completedMask;
            g_revealCount = g_sessState.revealCount < 32 ? g_sessState.revealCount : 32;
            for (int i = 0; i < g_revealCount; i++) {
                g_reveals[i].num = g_sessState.reveals[i].num;
                g_reveals[i].idx = g_sessState.reveals[i].idx;
            }
        }
        nvs_close(h);
    } else if (e == ESP_ERR_NVS_NOT_FOUND) {
        ok = true; // namespace never written yet => fresh session (do not retry forever)
    }
    // Any other error (e.g. ESP_ERR_NVS_NOT_INITIALIZED before nvs_flash_init) -> ok=false, retry.
    g_sessionDirty = false;
    LOG_I("VgSess", "load: e=%d ok=%d mask=0x%x reveals=%d", (int)e, (int)ok,
          (unsigned)g_persistedComplete, g_revealCount);
    sessionUnlock();
    return ok;
}

// Writes the current in-RAM state to NVS and commits it (durably flushed to flash before returning).
void sessionSaveToDisk() {
    sessionLock();
    g_sessState = SessionState {};
    g_sessState.completedMask = g_persistedComplete;
    g_sessState.revealCount = g_revealCount < 32 ? g_revealCount : 32;
    for (int i = 0; i < g_sessState.revealCount; i++) {
        g_sessState.reveals[i].num = g_reveals[i].num;
        g_sessState.reveals[i].idx = g_reveals[i].idx;
    }
    if (sessionSerialize(g_sessState, g_sessBuf, sizeof(g_sessBuf)) >= 0) {
        nvs_handle_t h;
        const esp_err_t e = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
        if (e == ESP_OK) {
            nvs_set_str(h, NVS_KEY, g_sessBuf);
            const esp_err_t c = nvs_commit(h); // durable flush to flash (survives an immediate reset)
            nvs_close(h);
            LOG_I("VgSess", "save: commit=%d mask=0x%x str='%s'", (int)c,
                  (unsigned)g_persistedComplete, g_sessBuf);
        } else {
            LOG_I("VgSess", "save: nvs_open failed e=%d", (int)e);
        }
    }
    g_sessionDirty = false;
    sessionUnlock();
}

void ensureSessionLoaded() {
    // Load once, but only latch "loaded" after a successful disk read, so an early call before the
    // /data filesystem is mounted retries on the next access rather than starting a blank session.
    if (!g_sessionLoaded && sessionLoadFromDisk()) {
        g_sessionLoaded = true;
    }
}
} // namespace

void sessionMarkComplete(int number) {
    ensureSessionLoaded();
    const uint32_t bit = sessionBit(number);
    if (bit == 0 || (g_persistedComplete & bit)) return; // out of range or already recorded
    g_persistedComplete |= bit;
    sessionSaveToDisk();
}

void sessionFlushIfDirty() {
    if (g_sessionDirty) sessionSaveToDisk();
}

void sessionPreload() { ensureSessionLoaded(); }

void revealHint(int num, int idx) {
    if (hintRevealed(num, idx)) return;
    if (g_revealCount < (int)(sizeof(g_reveals) / sizeof(g_reveals[0]))) {
        g_reveals[g_revealCount++] = { num, idx };
        sessionSaveToDisk(); // hint penalties must also survive a reboot
    }
}

int hintPenaltyFor(int num) {
    int p = 0;
    for (int i = 0; i < g_revealCount; i++) {
        if (g_reveals[i].num == num && g_reveals[i].idx >= 0 && g_reveals[i].idx < HINT_COUNT) {
            p += CHALLENGE_HINTS[g_reveals[i].idx].cost;
        }
    }
    return p;
}

int hintPenaltyTotal() {
    int p = 0;
    for (int i = 0; i < g_revealCount; i++) {
        if (g_reveals[i].idx >= 0 && g_reveals[i].idx < HINT_COUNT) {
            p += CHALLENGE_HINTS[g_reveals[i].idx].cost;
        }
    }
    return p;
}

int netScore() {
    const int n = earnedScore() - hintPenaltyTotal();
    return n < 0 ? 0 : n;
}

// The live status straight from the running services (no persistence layer).
static ChStatus challengeStatusLive(const ChallengeDef& c) {
    switch (c.kind) {
        case ChKind::NotImplemented:
            return ChStatus::NotImplemented;
        // Protocol challenges: "not started" until a client sends a frame (touched), "in progress"
        // once someone is interacting, "completed" once solved.
        case ChKind::Iec104: {
            auto& iec = Iec104Service::instance();
            if (!iec.running()) return ChStatus::NotStarted;
            if (iec.solved()) return ChStatus::Completed;
            return iec.touched() ? ChStatus::InProgress : ChStatus::NotStarted;
        }
        case ChKind::Sis: {
            auto& sis = SisService::instance();
            if (!sis.running()) return ChStatus::NotStarted;
            if (sis.solved()) return ChStatus::Completed;
            return sis.touched() ? ChStatus::InProgress : ChStatus::NotStarted;
        }
        case ChKind::Pump: {
            auto& pmp = PumpService::instance();
            if (!pmp.running()) return ChStatus::NotStarted;
            if (pmp.solved()) return ChStatus::Completed;
            return pmp.touched() ? ChStatus::InProgress : ChStatus::NotStarted;
        }
        case ChKind::Opcua: {
            auto& o = OpcUaService::instance();
            if (!o.running()) return ChStatus::NotStarted;
            if (o.solved()) return ChStatus::Completed;
            return o.touched() ? ChStatus::InProgress : ChStatus::NotStarted;
        }
        case ChKind::Dam: {
            auto& dm = DamService::instance();
            if (!dm.running()) return ChStatus::NotStarted;
            if (dm.solved()) return ChStatus::Completed;
            return dm.touched() ? ChStatus::InProgress : ChStatus::NotStarted;
        }
        case ChKind::Goose: {
            auto& g = GooseService::instance();
            if (!g.running()) return ChStatus::NotStarted;
            if (g.solved()) return ChStatus::Completed;
            return g.touched() ? ChStatus::InProgress : ChStatus::NotStarted;
        }
        // Defensive web submissions latch in DefService. Completed only on a correct finding - opening
        // a page never counts. The Filter-by-Source SQLi path keeps no interaction state, so #07 shows
        // NotStarted until the extracted flag is submitted and latches the finding.
        case ChKind::Forensics: {
            auto& d = DefService::instance();
            if (!d.running()) return ChStatus::NotStarted;
            return d.forensics().solved() ? ChStatus::Completed : ChStatus::NotStarted;
        }
        case ChKind::Trojan: {
            auto& d = DefService::instance();
            if (!d.running()) return ChStatus::NotStarted;
            if (d.trojan().solved()) return ChStatus::Completed;
            return d.trojan().restored() ? ChStatus::InProgress : ChStatus::NotStarted;
        }
        case ChKind::Device: {
            auto& d = DefService::instance();
            if (!d.running()) return ChStatus::NotStarted;
            return d.takeover().solved() ? ChStatus::Completed : ChStatus::NotStarted;
        }
        case ChKind::SnmpRecovery: {
            auto& s = SnmpService::instance();
            if (!s.running()) return ChStatus::NotStarted;
            if (s.solved()) return ChStatus::Completed;
            return s.touched() ? ChStatus::InProgress : ChStatus::NotStarted;
        }
        case ChKind::Reactor1:
        case ChKind::Reactor2: {
            // Reactor challenges share OtService. Status is informational; it never gates selection.
            auto& svc = OtService::instance();
            if (!svc.running()) return ChStatus::NotStarted;
            if (svc.challengeSolved(c.kind == ChKind::Reactor1 ? 1 : 2)) return ChStatus::Completed;
            return svc.networkStatus().clients > 0 ? ChStatus::InProgress : ChStatus::NotStarted;
        }
    }
    return ChStatus::NotStarted;
}

// Public status: a challenge counts as Completed if it was completed earlier (persisted/restored)
// OR is solved right now on its live service. Observing a fresh live completion latches it in RAM
// (the next save — completion edge, hint reveal, reset, or list refresh — writes it to disk), so a
// completed challenge never reverts and never re-awards its points after a reboot.
ChStatus challengeStatus(const ChallengeDef& c) {
    ensureSessionLoaded();
    const uint32_t bit = sessionBit(c.number);
    if (bit != 0 && (g_persistedComplete & bit) != 0) return ChStatus::Completed;
    ChStatus s = challengeStatusLive(c);
    if (bit != 0 && s == ChStatus::Completed) {
        g_persistedComplete |= bit;
        g_sessionDirty = true;
    }
    return s;
}

void startAllServices() {
    // Run the full bring-up exactly once, even if the boot controls task and the challenge menu both
    // reach here (CAS guards against a concurrent double-start creating duplicate tasks). Each
    // service's start() is itself idempotent, so later per-challenge start()s remain safe no-ops.
    static std::atomic<bool> brought_up{false};
    bool expected = false;
    if (!brought_up.compare_exchange_strong(expected, true)) return;

    OtService::instance().start(Challenge::One); // Wi-Fi AP + :80 web HMI + Modbus/TCP
    // #01 Tripping Breaker (Iec104Service/2404), #02 Safety Instrumented Bypass (SisService/1502) and
    // #04 Rogue Radio (PumpService/7700) were retired from the board: their listeners are not started,
    // so those ports stay closed. The backend code is kept but dormant.
    OpcUaService::instance().start();
    DefService::instance().start();   // shared Defensive investigation state (web :80)
    GooseService::instance().start(); // Second Blackout (training GOOSE/UDP 6180)
    SnmpService::instance().start();  // #08 Field-Device Recovery (SNMP/UDP 161)
}

void resetSession() {
    // Clear revealed hints (restores the hint-penalty component of the score to 0) and the latched
    // completion set, then persist the now-empty session so the cleared state also survives a reboot
    // (otherwise the old progress would come back on next boot from disk).
    ensureSessionLoaded();
    g_revealCount = 0;
    g_persistedComplete = 0;
    sessionSaveToDisk();

    // Reseed every challenge's solved state.
    // - OtService (reactor, Ch11/Ch12) reseeds in place, keeping the Wi-Fi AP + HTTP + Modbus up.
    // - The other challenge services reseed via stop()+start() (their start() reseeds the sim only
    //   when it was not already running). This briefly bounces their listener sockets but not the AP.
    //   stop() blocks ~400 ms each, so resetSession() must run on a worker task (see the header note).
    OtService::instance().resetChallenges();

    // #01/#02/#04 retired: their services are never started, so there is nothing to bounce here.
    OpcUaService::instance().stop();      OpcUaService::instance().start();
    GooseService::instance().stop();      GooseService::instance().start();
    SnmpService::instance().stop();       SnmpService::instance().start();
    DefService::instance().stop();        DefService::instance().start();
}

// ---- System-level completion signalling (see Challenges.h) -------------------------------------
// The completion screen used to be triggered only by the per-challenge view's own timer, so a solve
// that happened while the player was on another screen never showed it. The controls task now polls
// completedLiveMask() and, on a NEW completion, posts the challenge number; the (opened) challenge
// view renders the one shared Challenge Complete screen for it.
namespace { std::atomic<int> g_pendingComplete{0}; }

uint32_t completedLiveMask() {
    uint32_t m = 0;
    for (const auto& c : CHALLENGES) if (challengeStatus(c) == ChStatus::Completed) m |= (1u << c.number);
    for (const auto& c : DEFENSIVE)  if (challengeStatus(c) == ChStatus::Completed) m |= (1u << c.number);
    return m;
}
void postCompletion(int number) { g_pendingComplete.store(number); }
int pendingCompletion() { return g_pendingComplete.load(); }
void clearPendingCompletion(int number) { int e = number; g_pendingComplete.compare_exchange_strong(e, 0); }

} // namespace ot::ui
