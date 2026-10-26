// SPDX-License-Identifier: Apache-2.0
#include "CompleteLayout.h"

#include "RefUi.h"
#include "Theme.h"
#include "assets/TrophyImg.h"

#include <lvgl.h>
#include <lvgl/fonts.h>

#include <cstdio>

// Reference: UI designs/chall_completionpage.png. Reconstructed from measured geometry — see
// artifacts/ui/LAYOUT_SPEC.md. All composition is absolute-positioned inside the blue panel.
namespace ot::ui {

namespace {

namespace th = vanguard::theme;
namespace rf = ot::ui::ref;

inline lv_color_t blue() { return th::defensive(); }

// 220x52 A8 pixel-art emblem (cup + star cutout + handles + two-tier base + laurels + sparkles),
// recoloured to theme green. Crisp at integer pixels, negligible heap.
void drawTrophy(lv_obj_t* parent, int relY) {
    auto* im = lv_image_create(parent);
    lv_image_set_src(im, &trophy_emblem_img);
    lv_obj_set_style_image_recolor(im, th::green(), 0);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    lv_obj_align(im, LV_ALIGN_TOP_MID, 0, relY);
}

void scorePanel(lv_obj_t* parent, int x, int y, int w, int h,
                const char* caption, lv_color_t capColor, const char* value, lv_color_t valColor) {
    auto* p = rf::box(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_radius(p, 4, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_border_color(p, blue(), 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 2, 0);
    rf::label(p, caption, rf::fontTiny(), capColor);
    auto* v = rf::label(p, value, rf::fontBig(), valColor);
    // Tighten so a 4-digit "N/2500" total still fits the 152px panel without clipping.
    lv_obj_set_style_text_letter_space(v, -1, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_CLIP);
}

lv_obj_t* actionButton(lv_obj_t* parent, int x, int y, int w, int h,
                       const char* text, lv_color_t color, bool fill) {
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
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto* l = rf::label(b, text, rf::fontSmall(), color);
    lv_obj_set_style_text_letter_space(l, -1, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    return b;
}

} // namespace

CompleteButtons buildCompleteScreen(lv_obj_t* parent, bool offensive, const char* categoryTitle,
                                    const char* difficulty, int pointsAwarded, int netScore,
                                    int scoreMax, bool hasNext) {
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

    // Row 1: header + score chip (blue).
    rf::headerBar(root, netScore, scoreMax, blue());

    // Row 2: breadcrumb "> CATEGORY > NAME"  [ DIFF ]  POINTS: n
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
    char pts[16]; snprintf(pts, sizeof(pts), "POINTS: %d", pointsAwarded);
    rf::label(head, pts, rf::fontTiny(), th::textMuted());

    // Row 3: completion panel (fills the rest), blue-bordered, absolute children.
    auto* panel = rf::box(root);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, blue(), 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    const int CW = 312;
    drawTrophy(panel, 5);

    auto* hdgBox = rf::box(panel);
    lv_obj_set_size(hdgBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(hdgBox, 4, 0);
    lv_obj_set_style_border_width(hdgBox, 2, 0);
    lv_obj_set_style_border_color(hdgBox, th::green(), 0);
    lv_obj_set_style_pad_hor(hdgBox, 12, 0);
    lv_obj_set_style_pad_ver(hdgBox, 3, 0);
    auto* hdg = rf::label(hdgBox, "CHALLENGE COMPLETE", rf::fontBig(), th::green());
    lv_obj_set_style_text_letter_space(hdg, -3, 0);
    lv_obj_align(hdgBox, LV_ALIGN_TOP_MID, 0, 59);

    const char* msg = offensive ? "You breached the target and\ncaptured the control flag!"
                                 : "You successfully detected and\nresponded to the intrusion!";
    auto* ml = rf::label(panel, msg, rf::fontTiny(), th::text());
    lv_obj_set_width(ml, CW);
    lv_obj_set_style_text_align(ml, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_WRAP);
    lv_obj_align(ml, LV_ALIGN_TOP_MID, 0, 89);

    auto* sep = rf::box(panel);
    lv_obj_set_size(sep, 266, 2);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, blue(), 0);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 107);

    char awarded[8]; snprintf(awarded, sizeof(awarded), "+%d", pointsAwarded);
    char total[16]; snprintf(total, sizeof(total), "%d/%d", netScore, scoreMax);
    scorePanel(panel, 1, 137, 152, 34, "POINTS AWARDED", blue(), awarded, th::green());
    scorePanel(panel, 157, 137, 152, 34, "TOTAL SCORE", th::textMuted(), total, th::text());

    CompleteButtons btns;
    btns.next = actionButton(panel, 1, 174, 152, 21, hasNext ? "NEXT CHALLENGE >" : "NO MORE >",
                             th::green(), true);
    btns.list = actionButton(panel, 157, 174, 152, 21, "CHALLENGE LIST", blue(), false);
    return btns;
}

} // namespace ot::ui
