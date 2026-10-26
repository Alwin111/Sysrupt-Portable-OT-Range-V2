// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

// Vanguard UI theme: near-black background, bright cyan as the primary accent and neon green as
// the selection colour (whatever has focus is green). Shared by the system theme (SystemTheme.cpp)
// and all Vanguard apps, so everything looks like one product. Tuned for the ST7789 panel (RGB565).
namespace vanguard::theme {

// Everything is matte black; structure comes from the cyan borders + the coloured accents/text.
inline lv_color_t background() { return lv_color_hex(0x000000); }
inline lv_color_t surface() { return lv_color_hex(0x000000); }       // panels/tiles/rows
inline lv_color_t surfaceRaised() { return lv_color_hex(0x0A0A0A); } // gauge/bar tracks (barely lifted so they stay visible)
inline lv_color_t surfaceFocused() { return lv_color_hex(0x000000); }
inline lv_color_t border() { return lv_color_hex(0x5C1E18); }   // dark red (was teal)
inline lv_color_t accent() { return lv_color_hex(0xFF3B30); }   // red (was cyan)
inline lv_color_t accentDim() { return lv_color_hex(0xA3281E); }
inline lv_color_t green() { return lv_color_hex(0x3DFF8F); }
inline lv_color_t greenDim() { return lv_color_hex(0x1C9E5A); }
// Background of a selected item (list row, dropdown option). Black; the green focus border marks focus.
inline lv_color_t selected() { return lv_color_hex(0x000000); }
inline lv_color_t text() { return lv_color_hex(0xF0FBFF); }
inline lv_color_t textMuted() { return lv_color_hex(0xA89E98); } // neutral warm grey (was slate-blue)
inline lv_color_t textOnAccent() { return lv_color_hex(0x1A0000); }
inline lv_color_t warning() { return lv_color_hex(0xFFC14D); }
inline lv_color_t red() { return lv_color_hex(0xE8593C); }
// Reference-UI accents: Offensive = red (above), Defensive = blue, primary = green.
inline lv_color_t offensive() { return lv_color_hex(0xFF3B30); }
inline lv_color_t defensive() { return lv_color_hex(0x2E9BFF); }

constexpr int32_t RADIUS = 12;

}
