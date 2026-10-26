// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Theme.h"

#include <lvgl.h>
#include <lvgl/fonts.h>

// Reusable industrial-HMI instrumentation widgets (semicircular gauges, bar meters, stat tiles and
// titled panels), styled for the Vanguard dark/cyan theme and the 320x240 ST7789 panel. Header-only
// (all `inline`) so no new translation unit / CMake reconfigure is needed; include where drawn.
//
// These are deliberately generic and self-contained: each builder returns a small handle struct of
// the LVGL objects a caller needs to update live, and a matching `...Set()` updates value + colour.
namespace ot::ui::instr {

namespace th = vanguard::theme;

// --- shared helpers -------------------------------------------------------------------------------

inline lv_obj_t* plain(lv_obj_t* parent) {
    auto* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

inline lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, lv_color_t color) {
    auto* l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// A bordered industrial panel with a thin cyan top accent. Content flows as a centered column.
inline lv_obj_t* panel(lv_obj_t* parent, int w, int h) {
    auto* p = plain(parent);
    if (w > 0 || h > 0) lv_obj_set_size(p, w > 0 ? w : LV_SIZE_CONTENT, h > 0 ? h : LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, th::surface(), 0);
    lv_obj_set_style_radius(p, 6, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, th::border(), 0);
    lv_obj_set_style_pad_all(p, 5, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 2, 0);
    return p;
}

// --- semicircular gauge ---------------------------------------------------------------------------

struct Gauge {
    lv_obj_t* arc = nullptr;
    lv_obj_t* value = nullptr; // big number in the middle
    lv_obj_t* caption = nullptr;
};

// A 270-degree arc gauge with a caption above and a value read-out in the centre. `diameter` sizes
// the arc; keep it <=120 for the badge screen.
inline Gauge gauge(lv_obj_t* parent, const char* caption, int diameter) {
    Gauge g;
    auto* col = plain(parent);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 1, 0);

    g.caption = text(col, caption, &lv_font_unscii_8, th::textMuted());

    auto* arc = lv_arc_create(col);
    lv_obj_set_size(arc, diameter, diameter);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    // Display-only: never grab focus or input (otherwise it eats LEFT/RIGHT navigation).
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, th::surfaceRaised(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, th::green(), LV_PART_INDICATOR);
    // Hide the draggable knob entirely.
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    g.arc = arc;

    g.value = text(arc, "--", &lv_font_unscii_16, th::text());
    lv_obj_center(g.value);
    return g;
}

inline void gaugeSet(const Gauge& g, int pct, const char* valueText, lv_color_t color) {
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    lv_arc_set_value(g.arc, pct);
    lv_obj_set_style_arc_color(g.arc, color, LV_PART_INDICATOR);
    lv_label_set_text(g.value, valueText);
    lv_obj_set_style_text_color(g.value, color, 0);
    lv_obj_center(g.value);
}

// --- semicircular gauge (half-disc, opens downward) -----------------------------------------------
//
// A true 180-degree arc: endpoints sit on a horizontal diameter (9 o'clock -> 12 -> 3 o'clock), the
// bowl faces down, and the value reads out just inside the flat side with a caption underneath. This
// is the reusable "semi-circle" instrument; use it (not the 270-degree gauge above) wherever the
// design calls for a half-dial. Self-sizing: the wrapper is only (diameter/2 + text) tall, so the
// empty lower half of the arc's bounding square is clipped away and wastes no vertical LCD space.
struct SemiGauge {
    lv_obj_t* arc = nullptr;
    lv_obj_t* value = nullptr;   // big number inside the bowl
    lv_obj_t* caption = nullptr; // small label under the flat side
};

// `diameter` is the full width of the half-disc; keep it <= 150 for the 320x240 panel. `stroke` is
// the arc thickness (0 -> auto = diameter/9, clamped to [4,14]).
inline SemiGauge semiGauge(lv_obj_t* parent, const char* caption, int diameter, int stroke = 0) {
    SemiGauge g;
    if (stroke <= 0) { stroke = diameter / 9; if (stroke < 4) stroke = 4; if (stroke > 14) stroke = 14; }
    const int capH = 11;       // room for the caption under the diameter
    const int wrapH = diameter / 2 + capH + 4;

    auto* wrap = plain(parent);
    lv_obj_set_size(wrap, diameter, wrapH);
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE); // clip the empty bottom half of the arc square

    auto* arc = lv_arc_create(wrap);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 0);       // circle centre sits on the flat diameter line
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 180, 360);             // top half only (9 o'clock -> 3 o'clock)
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    // Display-only: never take focus/input (otherwise it eats LEFT/RIGHT navigation).
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);     // no backdrop circle
    lv_obj_set_style_arc_width(arc, stroke, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, th::surfaceRaised(), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, stroke, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, th::green(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);     // hide the draggable knob
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    g.arc = arc;

    // Value reads just above the flat diameter (inside the bowl).
    g.value = text(wrap, "--", &lv_font_unscii_16, th::text());
    lv_obj_align(g.value, LV_ALIGN_TOP_MID, 0, diameter / 2 - 20);
    // Caption under the flat side.
    g.caption = text(wrap, caption, &lv_font_unscii_8, th::textMuted());
    lv_obj_align(g.caption, LV_ALIGN_BOTTOM_MID, 0, 0);
    return g;
}

inline void semiGaugeSet(const SemiGauge& g, int pct, const char* valueText, lv_color_t color) {
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    lv_arc_set_value(g.arc, pct);
    lv_obj_set_style_arc_color(g.arc, color, LV_PART_INDICATOR);
    lv_label_set_text(g.value, valueText);
    lv_obj_set_style_text_color(g.value, color, 0);
}

// --- stat tile ------------------------------------------------------------------------------------

struct Tile {
    lv_obj_t* value = nullptr;
    lv_obj_t* caption = nullptr;
};

// A compact read-out box: a caption on top and a bold value below, colour-coded to status.
inline Tile tile(lv_obj_t* parent, const char* caption, int w, int h) {
    Tile t;
    auto* b = plain(parent);
    lv_obj_set_size(b, w != 0 ? w : LV_SIZE_CONTENT, h > 0 ? h : LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, th::surface(), 0);
    lv_obj_set_style_radius(b, 5, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, th::border(), 0);
    lv_obj_set_style_pad_hor(b, 7, 0);
    lv_obj_set_style_pad_ver(b, 4, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    t.caption = text(b, caption, &lv_font_unscii_8, th::textMuted());
    t.value = text(b, "--", &lv_font_unscii_16, th::text());
    return t;
}

inline void tileSet(const Tile& t, const char* value, lv_color_t color) {
    lv_label_set_text(t.value, value);
    lv_obj_set_style_text_color(t.value, color, 0);
}

// --- horizontal bar meter -------------------------------------------------------------------------

struct Bar {
    lv_obj_t* fill = nullptr;
    lv_obj_t* value = nullptr;
    lv_obj_t* caption = nullptr;
};

// A labelled horizontal bar (caption left, value right, track + fill below).
inline Bar bar(lv_obj_t* parent, const char* caption, int w) {
    Bar br;
    auto* col = plain(parent);
    lv_obj_set_size(col, w > 0 ? w : LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);

    auto* headRow = plain(col);
    lv_obj_set_size(headRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(headRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    br.caption = text(headRow, caption, &lv_font_unscii_8, th::textMuted());
    br.value = text(headRow, "--", &lv_font_unscii_8, th::text());

    auto* track = plain(col);
    lv_obj_set_size(track, LV_PCT(100), 8);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, th::surfaceRaised(), 0);
    lv_obj_set_style_radius(track, 4, 0);
    br.fill = plain(track);
    lv_obj_set_size(br.fill, LV_PCT(0), LV_PCT(100));
    lv_obj_set_style_bg_opa(br.fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(br.fill, th::green(), 0);
    lv_obj_set_style_radius(br.fill, 4, 0);
    return br;
}

inline void barSet(const Bar& b, int pct, const char* valueText, lv_color_t color) {
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    lv_obj_set_width(b.fill, LV_PCT(pct));
    lv_obj_set_style_bg_color(b.fill, color, 0);
    lv_label_set_text(b.value, valueText);
    lv_obj_set_style_text_color(b.value, color, 0);
}

} // namespace ot::ui::instr
