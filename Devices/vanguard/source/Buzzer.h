#pragma once

// Minimal navigation buzzer for the Vanguard badge's passive piezo on GPIO7
// (bi0s,vanguard.dts: "buzzer (7)"). Mirrors the inctf Vanguard firmware, which
// plays a short click on each joystick/select press. Driven by hardware LEDC PWM
// (a square wave is needed for a passive piezo) and auto-silenced by a one-shot
// esp_timer, so a click never blocks the caller (the LVGL keypad read callback).
namespace vanguard::buzzer {

// Configure the LEDC timer/channel and the auto-off timer. Idempotent; safe to
// call before the first click. A no-op (clicks stay silent) if setup fails.
void init();

// Emit the short navigation click. Safe to call from the LVGL task; returns
// immediately. Rapid calls simply restart the click.
void navClick();

// Play the short "challenge solved" success chime: a rising arpeggio mirroring
// the inctf Vanguard firmware's flag-accepted jingle. Non-blocking (the notes
// are sequenced by the auto-off timer), so it is safe to call from the LVGL
// task when the challenge-complete screen appears.
void playSuccess();

} // namespace vanguard::buzzer
