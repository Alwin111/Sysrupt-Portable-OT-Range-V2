// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// The challenge registry: the single source of truth for which challenges exist, their names,
// category, difficulty, points, story/objectives and access info. The category-select screen, the
// challenge list and the per-challenge overview all read this. Challenges are independently
// selectable; status is informational and never gates access.
namespace ot::ui {

enum class ChKind {
    Reactor1,       // 11 Stabilise the Reactor   - OtService (shared reactor, Modbus 502)
    Reactor2,       // 12 Maintenance Override     - OtService (shared reactor, Modbus 502)
    Iec104,         // 01 Tripping Breaker         - Iec104Service (2404)
    Sis,            // 02 Safety Instrumented Bypass - SisService (1502)
    Pump,           // 04 Rogue Radio              - PumpService (7700)
    Opcua,          // 03 OPC Scout                - OpcUaService (OPC-UA-lite, 9005)
    Dam,            // 05 Dial-In Dam              - DamService (modem emulator, TCP 9003)
    Goose,          // 06 Second Blackout          - GooseService (training GOOSE/UDP 6180)
    Forensics,      // 07 Historian Forensics      - DefService forensics submission (web :80) [Defensive]
    Trojan,         // 09 Trojan Historian         - DefService backup/restore (web :80)        [Defensive]
    Device,         // 10 Device Takeover          - DefService DCP identity audit (web :80)    [Defensive]
    SnmpRecovery,   // 08 Field-Device Recovery    - SnmpService (SNMP/UDP 161)                 [Defensive]
    NotImplemented  // no live backend (reserved)
};

enum class Category { Offensive, Defensive };
enum class Difficulty { Easy, Medium, Hard, Expert };

struct ChallengeDef {
    int number;
    const char* name;
    const char* brief;      // one-line, non-spoiler
    ChKind kind;
    Category category;
    Difficulty difficulty;
    int points;
    const char* story;      // short scenario (3-4 short lines)
    const char* objectives[4];
    int objectiveCount;
    const char* protocol;   // e.g. "MODBUS TCP"
    const char* access;     // e.g. "10.0.1.1:502"
};

// Offensive challenges (live catalogue 03, 06, 11, 12). Numbers follow the implementation spec.
// #01 Tripping Breaker, #02 Safety Instrumented Bypass and #04 Rogue Radio were retired from the
// board; their backend services are no longer started (see startAllServices / resetSession).
// Stable backend behaviour is unchanged; only the player-facing title/story/objectives were aligned
// to the spec and expanded for beginners. Each story names the site, the incident and the attacker role.
constexpr ChallengeDef CHALLENGES[] = {
    { 3, "OPC SCOUT", "Find and read the node the browse tree hides.", ChKind::Opcua,
      Category::Offensive, Difficulty::Medium, 150,
      "A monitoring server shares data over a custom OPC-UA-lite service (not standard OPC UA, non-standard "
      "port). One node was unlinked from the browse tree - find it and read it.",
      { "Read the monitoring email (10.0.1.1/mail)", "Browse the server from Root (Objects / Server)", "Read Diagnostics for the orphan NodeId", "Read the orphaned node directly" }, 4,
      "OPC-UA-LITE", "10.0.1.1:9005" },

    // #05 DIAL-IN DAM removed from the live catalogue (de-duplication): its learning objective -
    // reach a device over a simple protocol and SET a single value, then hold - was the least unique
    // in the offensive set and overlapped the new Defensive SNMP recovery (#08) read-modify-verify
    // loop. DamService/DamSystem (modem :9003) are kept as reference code but are no longer started.
    { 6, "SECOND BLACKOUT", "Forge a GOOSE breaker-open event the receiver accepts as genuine.", ChKind::Goose,
      Category::Offensive, Difficulty::Expert, 300,
      "Substation relays trust GOOSE multicast with no authentication. Observe the stream, then forge a breaker-open state change the receiver accepts. (Training sim over UDP.)",
      { "Observe the normal GOOSE stream (UDP 6180)", "Learn the APPID and confRev", "Forge the next stNum with breaker-open", "Send it so the relay trips" }, 4,
      "GOOSE (TRAINING UDP)", "10.0.1.1:6180" },

    { 11, "STABILISE THE REACTOR", "Bring temperature, pressure and flux into their safe bands and hold.", ChKind::Reactor1,
      Category::Offensive, Difficulty::Expert, 300,
      "A reactor runs outside its safe temp, pressure and flux ranges (Modbus). Drive all three back to safe and hold. "
      "Safe-band status is multi-bit, not a single pass/fail flag.",
      { "Follow robots.txt to /control", "Read the maintenance notice", "Map HMI values to Modbus registers", "Hold all three bands in their safe state for 5s" }, 4,
      "MODBUS TCP", "10.0.1.1:502" },

    { 12, "MAINTENANCE OVERRIDE", "Get the controller to accept and retain an unauthorised rod position.", ChKind::Reactor2,
      Category::Offensive, Difficulty::Expert, 300,
      "A maintenance rod is write-protected, and a reconciliation loop reverts any change it did not authorise. "
      "Get the controller to accept an unauthorised rod position - and make it keep that value.",
      { "Read the engineering notice", "See a locked write rejected", "Find the short unlock window the controller exposes", "Write the rod during that window and make it stick for 10s" }, 4,
      "MODBUS TCP", "10.0.1.1:502" },
};
constexpr int CHALLENGE_COUNT = (int)(sizeof(CHALLENGES) / sizeof(CHALLENGES[0]));

// Defensive challenges (canonical catalogue 07, 08, 09, 10), in numeric order. Forensics /
// investigation / recovery are Defensive per the spec. 07 reuses the historian backend; 08 is the
// SNMP field-device recovery (SnmpService, UDP 161); 09 and 10 ride the shared DefService web.
constexpr ChallengeDef DEFENSIVE[] = {
    { 7, "HISTORIAN SQL INJECTION", "Exploit the historian's SQL injection and extract the hidden flag.", ChKind::Forensics,
      Category::Defensive, Difficulty::Hard, 250,
      "A plant historian sits on the badge network, unlinked from the main site. Its Filter-by-Source query is injectable: find the console, extract the flag by SQL injection, then submit it.",
      { "Find the historian console (not on the main nav)", "Test Filter-by-Source for SQLi (try a single quote)", "Enumerate the tables and extract the hidden flag", "Submit the extracted flag (10.0.1.1/intranet)" }, 4,
      "WEB (SQLi)", "10.0.1.1/intranet" },

    { 8, "FIELD DEVICE RECOVERY", "Restore a tampered field RTU to its last-good firmware over SNMP.", ChKind::SnmpRecovery,
      Category::Defensive, Difficulty::Hard, 250,
      "Field RTU-07 is running a tampered firmware image after an incident. Investigate it over SNMP, then recover it to its own last-good image and verify it reports healthy again.",
      { "Query the RTU over SNMP (UDP 161, community public)", "Find it is unhealthy: running image != last-good image", "Enter recovery mode, restore the last-good image", "Return to run mode; verify it reads healthy" }, 4,
      "SNMP", "10.0.1.1:161" },

    { 9, "TROJAN HISTORIAN", "Restore one deleted historian record from the retained backup.", ChKind::Trojan,
      Category::Defensive, Difficulty::Medium, 150,
      "A trojanised historian deleted one TURBINE.VIB sample and masked it among comms-outage gaps in the "
      "live trend. The nightly backup (all tags) still holds the deleted record. Work out which record was "
      "deleted, restore it, and keep it stable.",
      { "Open the live + backup historian exports (webmail)", "Filter the backup to the live trend's tag and diff the two", "Restore the seq+value in the backup but missing from live (10.0.1.1/investigate)", "Keep it stable for 10s" }, 4,
      "WEB (MAIL + SUBMIT)", "10.0.1.1/mail" },

    { 10, "DEVICE TAKEOVER", "Identify the impostor device from saved PROFINET evidence.", ChKind::Device,
      Category::Defensive, Difficulty::Easy, 100,
      "A device impersonates a trusted station (PROFINET DCP). From the saved capture, inventory and OUI registry, find the impostor's MAC and submit it.",
      { "Open the saved DCP capture (attached in webmail)", "Find the name claimed by two MACs", "Check each MAC vs inventory + OUI registry", "Submit the impostor MAC" }, 4,
      "PROFINET DCP (EVIDENCE)", "10.0.1.1/mail" },
};
constexpr int DEFENSIVE_COUNT = (int)(sizeof(DEFENSIVE) / sizeof(DEFENSIVE[0]));

enum class ChStatus { NotStarted, InProgress, Completed, NotImplemented };

const char* statusLabel(ChStatus s);
const char* difficultyLabel(Difficulty d);
// Short display title (the internal `name` is preserved). Used everywhere the title is shown so the
// list / breadcrumb / overview / status stay consistent and leave room for larger body text.
const char* displayTitle(const ChallengeDef& c);
ChStatus challengeStatus(const ChallengeDef& c);
// Sum of points for solved offensive challenges, and the score-chip denominator.
int earnedScore();
// Total attainable points = every scorable challenge's points (offensive + defensive). Derived from
// the live catalogue, so it tracks any change to the per-challenge difficulty-based points without a
// second constant to keep in sync. Points are difficulty-weighted (Easy 100 / Medium 150 /
// Hard 200-250 / Expert 300), and sum to exactly 1800 across the 8 active challenges.
constexpr int totalAttainablePoints() {
    int s = 0;
    for (const auto& c : CHALLENGES) s += c.points;
    for (const auto& c : DEFENSIVE) s += c.points;
    return s;
}
constexpr int SCORE_MAX = totalAttainablePoints();

// Compile-time guards on the scoring catalogue (the authoritative definition of attainable points):
static_assert(SCORE_MAX == 1800, "Total attainable score must be exactly 1800 across the 8 challenges");
static_assert(CHALLENGE_COUNT + DEFENSIVE_COUNT == 8, "Exactly 8 active challenges expected");

// Hints: two tiers per challenge. Revealing one permanently deducts its cost from the score, so a
// hint is a real trade-off (e.g. #07's GET STARTED hands over the historian console's location, which
// the challenge brief no longer gives away). Costs apply to every challenge's two tiers (global).
struct HintDef { const char* title; int cost; };
constexpr HintDef CHALLENGE_HINTS[] = {
    { "GET STARTED", 10 },
    { "IMPORTANT LEAD", 20 },
};
constexpr int HINT_COUNT = (int)(sizeof(CHALLENGE_HINTS) / sizeof(CHALLENGE_HINTS[0]));

bool hintRevealed(int challengeNumber, int hintIndex);
void revealHint(int challengeNumber, int hintIndex);
int hintPenaltyFor(int challengeNumber); // points deducted for one challenge's revealed hints
int hintPenaltyTotal();                  // points deducted across all challenges
int netScore();                          // earnedScore() - hintPenaltyTotal(), floored at 0

// The menu stores the chosen challenge so the single view app knows which one to render.
void setSelectedChallenge(int number);
int selectedChallenge();
const ChallengeDef& challengeByNumber(int number);

// The next challenge after `currentNumber` within the SAME category (offensive or defensive), in
// registry order. Returns -1 when the current one is the last in its category (no next). Used by the
// Challenge Complete screen's NEXT CHALLENGE action. #08 is not in either array, so it is never
// selected.
int nextChallengeNumber(int currentNumber);

// Session reset: clears all revealed hints and reseeds every challenge's solved state back to the
// clean start (score returns to 0). Used by the Reset Session screen. NOTE: this stops/starts the
// per-challenge services (brief socket bounce) and so MUST be called from a worker task, not the
// LVGL/UI task. It does not drop the Wi-Fi AP.
void resetSession();

// Bring up every badge network service (Wi-Fi AP + :80 web HMI + Modbus + the defensive/protocol
// backends) so the range is attackable over the network from ANY LCD screen, not only the challenge
// menu. Runs the bring-up exactly once process-wide (race-safe), so calling it from both the boot
// controls task and the menu is harmless. Safe to call from a worker task (creates FreeRTOS tasks +
// acquires the Wi-Fi radio); the caller must ensure Tactility's Wi-Fi is already initialised.
void startAllServices();

// ---- Persisted session state (survives a plain ESP32 reboot; cleared only by resetSession()) ----
// Record a genuine challenge completion and write it to the internal /data store immediately, so a
// reboot restores it. Idempotent: a challenge already recorded is not written again and never
// re-awards its points. Called from the completion edge in the challenge view.
void sessionMarkComplete(int challengeNumber);
// Loads the persisted session from disk once (no-op after a successful load). Safe to call every
// loop iteration. Call it from a task with a generous stack (e.g. the controls task) so the one-time
// file read never happens deep inside an LVGL render on the smaller UI task stack.
void sessionPreload();
// Writes the session to disk only if a live completion has been observed since the last save (a
// cheap safety-net for challenges solved while their view was not open). Call from a periodic UI
// refresh. Does nothing when there is nothing new to persist.
void sessionFlushIfDirty();

// ---- System-level completion signalling (so the Challenge Complete screen shows from ANY screen) ----
// The controls task polls every challenge's live solved state and, on a NEW completion, records it and
// posts the challenge number here; whichever challenge view is (or is then) foreground shows the one
// shared Challenge Complete screen for it. This decouples "a challenge was solved" (detected over the
// network, on any screen) from "show the completion screen", fixing the case where a solve happened
// while the player was not on that challenge's own view.
uint32_t completedLiveMask();              // bit (1u << number) set for each currently-Completed challenge
void postCompletion(int number);           // the watcher posts a newly-completed challenge number
int pendingCompletion();                   // the challenge number awaiting its complete screen, or 0
void clearPendingCompletion(int number);   // the view clears it once the complete screen is shown

} // namespace ot::ui
