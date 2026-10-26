// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared builders for the Sysrupt OT Range V2 reference UI look (matte black, green primary, red
// Offensive, blue Defensive, bordered panels, header bar with a SCORE chip, footer control hints).
// Header-only inline so both the launcher (ChallengeMenu) and the offensive-list app can reuse it.
// The badge display is 320x240; the reference mockups are 4:3, so the composition maps 1:1 at low res.
#include "Theme.h"

#include <lvgl.h>
#include <lvgl/fonts.h>

#include <cstdio>

namespace ot::ui::ref {

namespace th = vanguard::theme;

// The reference UI uses a blocky pixel font. LVGL ships the unscii bitmap pixel fonts (enabled in
// lv_conf.h): unscii-16 for headers/body, unscii-8 for small text. Much closer to the reference than
// the default Montserrat-style vector font.
inline const lv_font_t* fontBig() { return &lv_font_unscii_16; }
inline const lv_font_t* fontSmall() { return &lv_font_unscii_8; }
// A condensed 5x8 pixel font (generated from Spleen) for dense screens where unscii-8's 8px advance
// is too wide - e.g. the challenge overview's full-word telemetry table, breadcrumb and footer.
extern "C" const lv_font_t sysrupt_font_5x8;
inline const lv_font_t* fontTiny() { return &sysrupt_font_5x8; }
// An even smaller 4x6 pixel font (Tom Thumb) for the densest grid - the telemetry table - so its
// columns stay narrow enough to leave the left panel room for its full text.
extern "C" const lv_font_t sysrupt_font_4x6;
inline const lv_font_t* fontMicro() { return &sysrupt_font_4x6; }

// ---- Typography roles (shared config; Phase-3 UI system) ------------------------------------------
// One semantic font system so screens stop hand-picking raw faces. Each role maps to one of the four
// bitmap faces the firmware ALREADY ships -- no new font library, no extra flash. The mapping matches
// the face each context currently uses, so adopting a role in place of the raw face is a zero-visual
// refactor that just makes the hierarchy explicit and consistent. Hierarchy (big -> dense):
//   fontBig   unscii-16  : app title, big numbers, the CHALLENGE COMPLETE banner
//   fontSmall unscii-8   : section headings and the readable body default
//   fontTiny  5x8 Spleen : dense rows -- status, hints, breadcrumb, two-column detail, nav
//   fontMicro 4x6 ThumB  : the telemetry grid only (densest tabular data)
inline const lv_font_t* fontTitle()       { return fontBig();   }  // 1. main screen / app title
inline const lv_font_t* fontChallenge()   { return fontSmall(); }  // 2. challenge number + title
inline const lv_font_t* fontHeading()     { return fontSmall(); }  // 3. section headings
inline const lv_font_t* fontBody()        { return fontSmall(); }  // 4. normal body text
inline const lv_font_t* fontHint()        { return fontTiny();  }  // 5. system hints
inline const lv_font_t* fontStatusLabel() { return fontTiny();  }  // 6. status labels
inline const lv_font_t* fontStatusValue() { return fontTiny();  }  // 7. status values
inline const lv_font_t* fontNav()         { return fontTiny();  }  // 8. footer / nav hints
inline const lv_font_t* fontError()       { return fontSmall(); }  // 9. error / invalid-input text
inline const lv_font_t* fontSuccess()     { return fontBig();   }  // 10. completion / success
inline const lv_font_t* fontTable()       { return fontMicro(); }  // telemetry grid (densest)

inline lv_obj_t* box(lv_obj_t* parent) {
    auto* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

inline lv_obj_t* label(lv_obj_t* parent, const char* t, const lv_font_t* font, lv_color_t color) {
    auto* l = lv_label_create(parent);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// A bordered panel in the given accent colour (the reference's bracketed boxes, approximated).
inline lv_obj_t* panel(lv_obj_t* parent, lv_color_t accent, int w, int h) {
    auto* p = box(parent);
    if (w) lv_obj_set_width(p, w); else lv_obj_set_width(p, LV_SIZE_CONTENT);
    if (h) lv_obj_set_height(p, h); else lv_obj_set_height(p, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, th::background(), 0);
    lv_obj_set_style_radius(p, 4, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_border_color(p, accent, 0);
    lv_obj_set_style_pad_all(p, 5, 0);
    return p;
}

// The top header: "SYSRUPT OT RANGE V2" (green) on the left and a "SCORE: n/100" chip on the right.
// Returns the score value label so the caller can update it live; `chipColor` tints the chip border.
inline lv_obj_t* headerBar(lv_obj_t* parent, int score, int scoreMax, lv_color_t chipColor) {
    auto* head = box(parent);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(head, 2, 0);
    lv_obj_set_style_border_color(head, th::green(), 0);
    lv_obj_set_style_pad_bottom(head, 2, 0);

    auto* title = label(head, "SYSRUPT OT RANGE V2", fontBig(), th::green());
    // unscii-16 has a wide 16px advance, so the 19-char title would clip at 304px. Tighten the
    // letter spacing (glyphs are only 12-14px wide) so the full name fits beside the score chip.
    lv_obj_set_style_text_letter_space(title, -3, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_width(title, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);

    auto* chip = box(head);
    lv_obj_set_flex_grow(chip, 0);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(chip, 3, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, chipColor, 0);
    lv_obj_set_style_pad_hor(chip, 3, 0);
    lv_obj_set_style_pad_ver(chip, 1, 0);
    // Compact "n/2500" only (the word SCORE is dropped). fontTiny keeps the 4-digit "2500/2500"
    // chip narrow enough that the full fontBig title still fits beside it at 320px.
    char s[16];
    snprintf(s, sizeof(s), "%d/%d", score, scoreMax);
    auto* val = label(chip, s, fontTiny(), th::green());
    return val;
}

// A single footer control hint ("[KEY] action") with the key in `keyColor` and the action white.
inline void footerHint(lv_obj_t* parent, const char* key, const char* action, lv_color_t keyColor) {
    auto* grp = box(parent);
    lv_obj_set_size(grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(grp, 3, 0);
    label(grp, key, fontSmall(), keyColor);
    label(grp, action, fontSmall(), th::textMuted());
}

// The footer control-hint bar (row of [KEY] action hints), as the references show along the bottom.
inline lv_obj_t* footerBar(lv_obj_t* parent) {
    auto* f = box(parent);
    lv_obj_set_width(f, LV_PCT(100));
    lv_obj_set_height(f, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(f, 2, 0);
    return f;
}

} // namespace ot::ui::ref
