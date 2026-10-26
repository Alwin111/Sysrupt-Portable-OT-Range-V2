#include "Buzzer.h"

#include <driver/ledc.h>
#include <esp_timer.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace vanguard::buzzer {
namespace {

constexpr int BUZZER_GPIO = 7; // passive piezo (bi0s,vanguard.dts: "buzzer (7)")

constexpr ledc_mode_t MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t TIMER = LEDC_TIMER_0;
constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_0;
constexpr ledc_timer_bit_t DUTY_RES = LEDC_TIMER_10_BIT;

// Mirrors the inctf Vanguard navigation click (kNavClick = 700 Hz, ~12 ms).
constexpr uint32_t NAV_CLICK_FREQ_HZ = 700;
constexpr int64_t NAV_CLICK_US = 12000;
constexpr uint32_t DUTY_ON = 512; // ~50% of a 10-bit range -> square wave

// "Challenge solved" chime: the inctf Vanguard per-challenge completion melody
// (challenge_engine.cpp kCompletionMelody) -- a short rising arpeggio.
struct Note {
    uint32_t freq_hz;
    uint32_t ms;
};
constexpr Note SUCCESS_MELODY[] = {
    {1318, 90}, {1568, 90}, {1976, 90}, {2637, 220},
};
constexpr size_t SUCCESS_LEN = sizeof(SUCCESS_MELODY) / sizeof(SUCCESS_MELODY[0]);

esp_timer_handle_t s_off_timer = nullptr;  // silences a one-shot nav click
esp_timer_handle_t s_note_timer = nullptr; // advances the success melody
std::atomic<bool> s_ready{false};

// Melody playback state (written from the LVGL task, advanced from the timer
// task; a torn read at worst drops/repeats one note of a buzzer chime).
const Note* s_seq = nullptr;
size_t s_seq_len = 0;
size_t s_seq_idx = 0;

void silence() {
    ledc_set_duty(MODE, CHANNEL, 0);
    ledc_update_duty(MODE, CHANNEL);
}

void off_cb(void*) {
    silence();
}

// Drive one note (frequency + tone on) and arm the timer that ends it. Past the
// last note, fall silent.
void startNote(size_t i) {
    if (s_seq == nullptr || i >= s_seq_len) {
        silence();
        return;
    }
    const Note& n = s_seq[i];
    ledc_set_freq(MODE, TIMER, n.freq_hz);
    ledc_set_duty(MODE, CHANNEL, DUTY_ON);
    ledc_update_duty(MODE, CHANNEL);
    esp_timer_stop(s_note_timer);
    esp_timer_start_once(s_note_timer, static_cast<int64_t>(n.ms) * 1000);
}

void note_cb(void*) {
    startNote(++s_seq_idx);
}

} // namespace

void init() {
    if (s_ready.load()) {
        return;
    }

    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = MODE;
    timer_cfg.duty_resolution = DUTY_RES;
    timer_cfg.timer_num = TIMER;
    timer_cfg.freq_hz = NAV_CLICK_FREQ_HZ;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        return;
    }

    ledc_channel_config_t channel_cfg = {};
    channel_cfg.gpio_num = BUZZER_GPIO;
    channel_cfg.speed_mode = MODE;
    channel_cfg.channel = CHANNEL;
    channel_cfg.timer_sel = TIMER;
    channel_cfg.duty = 0; // start silent
    channel_cfg.hpoint = 0;
    if (ledc_channel_config(&channel_cfg) != ESP_OK) {
        return;
    }

    esp_timer_create_args_t off_args = {};
    off_args.callback = &off_cb;
    off_args.name = "buzzer_off";
    if (esp_timer_create(&off_args, &s_off_timer) != ESP_OK) {
        return;
    }

    esp_timer_create_args_t note_args = {};
    note_args.callback = &note_cb;
    note_args.name = "buzzer_note";
    if (esp_timer_create(&note_args, &s_note_timer) != ESP_OK) {
        return;
    }

    s_ready.store(true);
}

void navClick() {
    if (!s_ready.load()) {
        return;
    }
    // Cancel any melody in flight and restore the click frequency (a melody
    // leaves the timer tuned to its last note).
    esp_timer_stop(s_note_timer);
    s_seq = nullptr;
    ledc_set_freq(MODE, TIMER, NAV_CLICK_FREQ_HZ);
    // Start the tone, then (re)arm the one-shot that silences it.
    ledc_set_duty(MODE, CHANNEL, DUTY_ON);
    ledc_update_duty(MODE, CHANNEL);
    esp_timer_stop(s_off_timer); // harmless if not currently running
    esp_timer_start_once(s_off_timer, NAV_CLICK_US);
}

void playSuccess() {
    if (!s_ready.load()) {
        return;
    }
    // Hand the channel over to the melody sequencer: cancel a pending nav click,
    // load the sequence, and fire the first note.
    esp_timer_stop(s_off_timer);
    s_seq = SUCCESS_MELODY;
    s_seq_len = SUCCESS_LEN;
    s_seq_idx = 0;
    startNote(0);
}

} // namespace vanguard::buzzer
