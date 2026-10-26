// SPDX-License-Identifier: Apache-2.0
#include "ResetLayout.h"

#include "RefUi.h"
#include "Theme.h"
#include "assets/ResetIcons.h"

#include <lvgl.h>
#include <lvgl/fonts.h>

#include <cstdio>

// Reference: UI designs/reset_page.png. Reconstructed from measured geometry — see
// artifacts/ui/LAYOUT_SPEC.md. Absolute-positioned composition inside the blue panel.
namespace ot::ui {

namespace {

namespace th = vanguard::theme;
namespace rf = ot::ui::ref;

inline lv_color_t blue() { return th::defensive(); }
inline lv_color_t red() { return th::red(); }

lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* src, lv_color_t color) {
    auto* im = lv_image_create(parent);
    lv_image_set_src(im, src);
    lv_obj_set_style_image_recolor(im, color, 0);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    return im;
}

// A horizontal red dashed rule: 4px dashes, 3px gaps.
void dashRule(lv_obj_t* parent, int x, int y, int totalW, lv_color_t color) {
    for (int dx = 0; dx < totalW; dx += 7) {
        auto* d = rf::box(parent);
        int w = (dx + 4 <= totalW) ? 4 : (totalW - dx);
        lv_obj_set_pos(d, x + dx, y);
        lv_obj_set_size(d, w, 2);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, color, 0);
    }
}

// A small right-pointing filled triangular bullet (5x5), anchored at (x,y).
void triBullet(lv_obj_t* parent, int x, int y, lv_color_t color) {
    static const int wrow[5] = { 1, 3, 5, 3, 1 };
    for (int i = 0; i < 5; i++) {
        auto* r = rf::box(parent);
        lv_obj_set_pos(r, x, y + i);
        lv_obj_set_size(r, wrow[i], 1);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, color, 0);
    }
}

void bulletRow(lv_obj_t* parent, int y, const char* text) {
    triBullet(parent, 2, y + 1, th::red());
    auto* l = rf::label(parent, text, rf::fontTiny(), th::text());
    lv_obj_set_pos(l, 12, y);
}

// RESET / CANCEL button: icon on the left, bold top line + muted sub line stacked on the right.
lv_obj_t* actionButton(lv_obj_t* parent, int x, int y, int w, int h, const lv_image_dsc_t* ic,
                       const char* top, const char* sub, lv_color_t color, bool fill) {
    auto* b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, color, 0);
    if (fill) {
        lv_obj_set_style_bg_opa(b, 40, 0);
        lv_obj_set_style_bg_color(b, color, 0);
    }
    lv_obj_set_style_border_width(b, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(b, th::green(), LV_STATE_FOCUS_KEY);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 7, 0);
    icon(b, ic, color);
    auto* txt = rf::box(b);
    lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(txt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    auto* t = rf::label(txt, top, rf::fontSmall(), color);
    lv_obj_set_style_text_letter_space(t, -1, 0);
    rf::label(txt, sub, rf::fontTiny(), th::textMuted());
    return b;
}

void footerGroup(lv_obj_t* parent, const char* key, const char* act, lv_color_t keyColor) {
    auto* g = rf::box(parent);
    lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g, 3, 0);
    rf::label(g, key, rf::fontTiny(), keyColor);
    rf::label(g, act, rf::fontTiny(), th::text());
}

} // namespace

ResetButtons buildResetScreen(lv_obj_t* parent, bool offensive, const char* categoryTitle,
                              const char* difficulty, int pointsValue, int netScore, int scoreMax) {
    const lv_color_t catColor = offensive ? th::offensive() : th::defensive();

    auto* root = rf::box(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, th::background(), 0);
    lv_obj_set_style_pad_hor(root, 3, 0);
    lv_obj_set_style_pad_top(root, 3, 0);
    lv_obj_set_style_pad_bottom(root, 2, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 2, 0);

    rf::headerBar(root, netScore, scoreMax, blue());

    // Metadata row.
    auto* head = rf::box(root);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 4, 0);
    char crumb[20]; snprintf(crumb, sizeof(crumb), "> %s >", offensive ? "OFFENSIVE" : "DEFENSIVE");
    rf::label(head, crumb, rf::fontTiny(), catColor);
    auto* nm = rf::label(head, categoryTitle, rf::fontSmall(), th::text());
    lv_obj_set_flex_grow(nm, 1); lv_obj_set_width(nm, 0);
    lv_obj_set_style_text_letter_space(nm, -1, 0);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_CLIP);
    auto* diffBox = rf::box(head);
    lv_obj_set_size(diffBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(diffBox, 3, 0);
    lv_obj_set_style_border_width(diffBox, 1, 0);
    lv_obj_set_style_border_color(diffBox, blue(), 0);
    lv_obj_set_style_pad_hor(diffBox, 4, 0);
    rf::label(diffBox, difficulty, rf::fontTiny(), th::green());
    char pts[16]; snprintf(pts, sizeof(pts), "POINTS: %d", pointsValue);
    rf::label(head, pts, rf::fontTiny(), th::textMuted());

    // Main panel (blue), absolute children.
    auto* panel = rf::box(root);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, blue(), 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Warning row: triangle + "RESET SESSION?" centred.
    auto* wrow = rf::box(panel);
    lv_obj_set_size(wrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wrow, 10, 0);
    icon(wrow, &warn_tri_img, red());
    auto* ttl = rf::label(wrow, "RESET SESSION?", rf::fontBig(), red());
    lv_obj_set_style_text_letter_space(ttl, -2, 0);
    lv_obj_align(wrow, LV_ALIGN_TOP_MID, 0, 5);

    // Red dashed separator under the heading.
    dashRule(panel, 22, 38, 266, red());

    // Two-line warning text.
    auto* warn = rf::label(panel,
        "This will reset the entire OT Range session.\nAll challenges, progress and scores will be lost.",
        rf::fontTiny(), th::text());
    lv_obj_set_width(warn, 300);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
    lv_obj_align(warn, LV_ALIGN_TOP_MID, 0, 44);

    // Red summary panel.
    auto* sum = rf::box(panel);
    lv_obj_set_pos(sum, 6, 62);
    lv_obj_set_size(sum, 297, 74);
    lv_obj_set_style_radius(sum, 4, 0);
    lv_obj_set_style_border_width(sum, 2, 0);
    lv_obj_set_style_border_color(sum, red(), 0);
    lv_obj_set_style_pad_all(sum, 0, 0);
    lv_obj_remove_flag(sum, LV_OBJ_FLAG_SCROLLABLE);
    auto* sh = rf::label(sum, "THE FOLLOWING WILL BE RESET:", rf::fontTiny(), red());
    lv_obj_set_pos(sh, 6, 4);
    dashRule(sum, 6, 14, 283, red());
    const char* items[5] = {
        "All offensive and defensive challenges",
        "All progress and completed objectives",
        "All earned points",
        "All hints usage",
        "Return to challenge selection",
    };
    for (int i = 0; i < 5; i++) bulletRow(sum, 20 + i * 10, items[i]);

    // Buttons (icon + stacked text).
    ResetButtons btns;
    btns.reset = actionButton(panel, 5, 141, 148, 24, &refresh_img, "RESET SESSION",
                              "YES, RESET EVERYTHING", th::green(), false);
    btns.cancel = actionButton(panel, 159, 141, 148, 24, &xmark_img, "CANCEL",
                               "KEEP CURRENT PROGRESS", red(), false);

    // Footer hint strip (blue top border), three groups split evenly.
    auto* foot = rf::box(root);
    lv_obj_set_width(foot, LV_PCT(100));
    lv_obj_set_height(foot, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(foot, 2, 0);
    lv_obj_set_style_border_color(foot, blue(), 0);
    lv_obj_set_style_pad_top(foot, 2, 0);
    lv_obj_set_style_pad_hor(foot, 2, 0);
    auto divider = [&]() {
        auto* d = rf::box(foot);
        lv_obj_set_size(d, 2, 11);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, blue(), 0);
    };
    footerGroup(foot, "[LEFT/RIGHT]", "SELECT", th::green());
    divider();
    footerGroup(foot, "[SELECT]", "CONFIRM", th::green());
    divider();
    footerGroup(foot, "[BACK]", "RETURN", red());
    return btns;
}

} // namespace ot::ui
