// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

// The Sysrupt OT Range display palette, reproduced for the badge's 320x240 ST7789. Retro-arcade:
// black background, phosphor green, hazard red/amber, monospace feel. Matches
// display/templates/display.html from the Sysrupt project.
namespace ot::retro {

inline lv_color_t black() { return lv_color_hex(0x000000); }
inline lv_color_t red() { return lv_color_hex(0xE8593C); }    // logo / hazard
inline lv_color_t green() { return lv_color_hex(0x00FF41); }  // phosphor green
inline lv_color_t amber() { return lv_color_hex(0xFFB000); }  // prompts / current
inline lv_color_t pink() { return lv_color_hex(0xFF0066); }   // alerts / HP label
inline lv_color_t white() { return lv_color_hex(0xFFFFFF); }
inline lv_color_t grayLight() { return lv_color_hex(0x888888); }
inline lv_color_t grayMid() { return lv_color_hex(0x555555); }
inline lv_color_t grayDark() { return lv_color_hex(0x333333); }
inline lv_color_t panel() { return lv_color_hex(0x0A0A0A); }

} // namespace ot::retro
