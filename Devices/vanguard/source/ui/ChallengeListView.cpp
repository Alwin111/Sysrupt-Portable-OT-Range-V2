// SPDX-License-Identifier: Apache-2.0
#include "ChallengeListView.h"

#include "ChallengeView.h"
#include "Challenges.h"
#include "RefUi.h"
#include "SystemTheme.h"
#include "Theme.h"

#include <app/start.h>

#include <lvgl.h>
#include <lvgl/fonts.h>

#include <cstdio>

// Reference: offensive-list.png (and the matching defensive list). Left = the challenges (name +
// difficulty, coloured by status); right = the selected challenge's story / objectives / points /
// status. UP/DOWN navigate, SELECT opens the overview, BACK returns to the category screen.
namespace ot::ui {

namespace {

namespace th = vanguard::theme;
namespace rf = ot::ui::ref;

constexpr int MAX_ROWS = 16;

struct ListView {
    const ChallengeDef* arr = nullptr;
    int count = 0;
    lv_color_t accent = th::offensive();
    lv_obj_t* rows[MAX_ROWS] = {};
    // detail panel widgets
    lv_obj_t* dName = nullptr;
    lv_obj_t* dDiff = nullptr;
    lv_obj_t* dStory = nullptr;
    lv_obj_t* dObj = nullptr;
    lv_obj_t* dPoints = nullptr;
    lv_obj_t* dStatus = nullptr;
    lv_obj_t* scoreVal = nullptr;
    lv_timer_t* timer = nullptr;
    int selected = 0;
};

lv_color_t statusColor(ChStatus s, lv_color_t accent) {
    switch (s) {
        case ChStatus::Completed: return th::green();
        case ChStatus::InProgress: return th::warning();
        case ChStatus::NotImplemented: return th::textMuted();
        default: return accent;
    }
}

void showDetail(ListView* v, int idx) {
    const ChallengeDef& c = v->arr[idx];
    lv_label_set_text(v->dName, displayTitle(c));          // full name (wraps, always fully visible)
    char diff[16]; snprintf(diff, sizeof(diff), "[ %s ]", difficultyLabel(c.difficulty));
    lv_label_set_text(v->dDiff, diff);
    lv_label_set_text(v->dStory, c.brief);        // short one-line description (preview on scroll)
    char pts[20]; snprintf(pts, sizeof(pts), "POINTS: %d", c.points);
    lv_label_set_text(v->dPoints, pts);
    const ChStatus st = challengeStatus(c);
    char stat[20]; snprintf(stat, sizeof(stat), "STATUS: %s", statusLabel(st)); // same wording as the STATUS tab
    lv_label_set_text(v->dStatus, stat);
    lv_obj_set_style_text_color(v->dStatus, statusColor(st, v->accent), 0);
}

void onRowFocused(lv_event_t* e) {
    auto* v = static_cast<ListView*>(lv_event_get_user_data(e));
    auto* row = lv_event_get_target_obj(e);
    const int idx = (int)reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row));
    v->selected = idx;
    showDetail(v, idx);
}

void onRowClicked(lv_event_t* e) {
    auto* v = static_cast<ListView*>(lv_event_get_user_data(e));
    auto* row = lv_event_get_target_obj(e);
    const int idx = (int)reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row));
    setSelectedChallenge(v->arr[idx].number);
    AppStartContext ctx; AppInstanceId id = 0;
    if (app_start_context_from_id("vanguard.challenge", &ctx) == ERROR_NONE) {
        app_start_with_context(&ctx, &id);
    }
}

// A full-width dashed divider rule (label of dashes, clipped to the parent width).
lv_obj_t* dash(lv_obj_t* parent, lv_color_t color) {
    auto* d = rf::label(parent, "----------------------------------------", rf::fontSmall(), color);
    lv_obj_set_width(d, LV_PCT(100));
    lv_label_set_long_mode(d, LV_LABEL_LONG_CLIP);
    return d;
}

lv_obj_t* makeRow(ListView* v, lv_obj_t* parent, int idx) {
    const ChallengeDef& c = v->arr[idx];
    auto* row = lv_button_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_user_data(row, reinterpret_cast<void*>((uintptr_t)idx));
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 3, 0);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, v->accent, 0);
    lv_obj_set_style_border_width(row, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(row, th::green(), LV_STATE_FOCUS_KEY);

    // ">" caret + name (takes the full remaining width). The difficulty is shown in the detail box,
    // not here, so the name gets the whole row (it still clips if longer than the column).
    rf::label(row, ">", rf::fontSmall(), v->accent);
    auto* name = rf::label(row, displayTitle(c), rf::fontSmall(), th::text());
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_width(name, 0);
    lv_obj_set_style_text_letter_space(name, -1, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);

    lv_obj_add_event_cb(row, onRowFocused, LV_EVENT_FOCUSED, v);
    lv_obj_add_event_cb(row, onRowClicked, LV_EVENT_SHORT_CLICKED, v);
    v->rows[idx] = row;
    return row;
}

void refresh(lv_timer_t* timer) {
    auto* v = static_cast<ListView*>(lv_timer_get_user_data(timer));
    if (v->scoreVal) { char s[16]; snprintf(s, sizeof(s), "%d/%d", netScore(), SCORE_MAX); lv_label_set_text(v->scoreVal, s); }
    showDetail(v, v->selected); // keep status live
    sessionFlushIfDirty();      // persist any completion observed without its challenge view open
}

void onRootDeleted(lv_event_t* e) {
    auto* v = static_cast<ListView*>(lv_event_get_user_data(e));
    if (v->timer) lv_timer_delete(v->timer);
    delete v;
}

} // namespace

void buildChallengeList(lv_obj_t* parent, const ChallengeDef* arr, int count,
                        lv_color_t accent, const char* category) {
    th::installSystemTheme(lv_obj_get_display(parent));
    auto* v = new ListView();
    v->arr = arr;
    v->count = count < MAX_ROWS ? count : MAX_ROWS;
    v->accent = accent;

    auto* root = rf::box(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, th::background(), 0);
    lv_obj_set_style_pad_all(root, 6, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 3, 0);
    lv_obj_add_event_cb(root, onRootDeleted, LV_EVENT_DELETE, v);

    v->scoreVal = rf::headerBar(root, netScore(), SCORE_MAX, accent);
    char crumb[20]; snprintf(crumb, sizeof(crumb), "> %s", category);
    rf::label(root, crumb, rf::fontSmall(), accent);

    // two columns
    auto* cols = rf::box(root);
    lv_obj_set_width(cols, LV_PCT(100));
    lv_obj_set_flex_grow(cols, 1);
    lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cols, 5, 0);

    // left: list
    auto* list = rf::box(cols);
    lv_obj_set_height(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 6);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_t* first = nullptr;
    for (int i = 0; i < v->count; i++) {
        auto* r = makeRow(v, list, i);
        if (i == 0) first = r;
    }

    // right: detail panel (scrollable so long stories/objectives don't overflow the box)
    auto* d = rf::panel(cols, accent, 0, LV_PCT(100));
    lv_obj_set_flex_grow(d, 5);
    lv_obj_set_flex_flow(d, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d, 2, 0);
    lv_obj_set_scroll_dir(d, LV_DIR_VER);
    // Full challenge name, wrapped so the whole name is always visible (the list rows clip it).
    v->dName = rf::label(d, "", rf::fontSmall(), accent);
    lv_obj_set_width(v->dName, LV_PCT(100));
    lv_obj_set_style_text_letter_space(v->dName, -1, 0);
    lv_label_set_long_mode(v->dName, LV_LABEL_LONG_WRAP);
    // Difficulty (left) + points (right). The row must size to its content height (without this it
    // took the default object height, which broke the detail panel's column layout). The narrow
    // detail column also only fits "[ HARD ]" + "POINTS: 10" in the condensed 5x8 font.
    auto* dhead = rf::box(d);
    lv_obj_set_width(dhead, LV_PCT(100));
    lv_obj_set_height(dhead, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dhead, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dhead, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    v->dDiff = rf::label(dhead, "", rf::fontTiny(), th::green());
    v->dPoints = rf::label(dhead, "POINTS: 0", rf::fontTiny(), th::green());
    dash(d, accent);
    // Short description (updates live as you scroll the list - no need to open the challenge).
    rf::label(d, "DESCRIPTION", rf::fontSmall(), accent);
    v->dStory = rf::label(d, "", rf::fontSmall(), th::text());
    lv_label_set_long_mode(v->dStory, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(v->dStory, LV_PCT(100));
    dash(d, accent);
    // Condensed so "STATUS: NOT STARTED" fits the narrow detail column without clipping.
    v->dStatus = rf::label(d, "", rf::fontTiny(), th::text());
    lv_obj_set_width(v->dStatus, LV_PCT(100));
    lv_label_set_long_mode(v->dStatus, LV_LABEL_LONG_WRAP);

    // Section footer box: centered "SELECT A CHALLENGE" prompt.
    auto* info = rf::panel(root, accent, LV_PCT(100), 0);
    lv_obj_set_style_pad_ver(info, 2, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    rf::label(info, "SELECT A CHALLENGE", rf::fontSmall(), th::green());

    // footer hints (same wording/keys as the challenge screen, short enough to never overlap)
    auto* foot = rf::footerBar(root);
    rf::footerHint(foot, "[<>]", "MOVE", th::green());
    rf::footerHint(foot, "[SEL]", "SELECT", th::green());
    rf::footerHint(foot, "[BACK]", "BACK", accent);

    showDetail(v, 0);
    v->timer = lv_timer_create(refresh, 800, v);
    if (first) {
        lv_obj_update_layout(root);
        lv_group_focus_obj(first);
        lv_obj_add_state(first, LV_STATE_FOCUS_KEY);
    }
}

} // namespace ot::ui
