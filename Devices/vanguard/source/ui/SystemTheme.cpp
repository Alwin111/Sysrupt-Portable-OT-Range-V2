// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For lv_theme_t's font fields
#include "SystemTheme.h"
#include "Theme.h"

#include <tactility/log.h>

// How it works:
// 1. LVGL's default theme is re-initialized with the Vanguard accent colours. Its styles are shared
//    by every widget, so this recolours all existing and future standard controls at once.
// 2. The default theme has fixed background/panel colours, so a Vanguard theme is layered on top of
//    it (LVGL theme inheritance, as Tactility does for the mono theme): its apply callback adds the
//    Vanguard surface styles. These are theme styles, so styles an app sets itself still win.
// 3. The status bar sets its colours as local styles, so it's restyled directly.
namespace vanguard::theme {

namespace {

constexpr auto* TAG = "SysruptTheme";

lv_theme_t vanguardTheme;

bool stylesInitialized = false;
lv_style_t screenStyle;
lv_style_t panelStyle;
lv_style_t focusOutlineStyle;
lv_style_t headerStyle;
lv_style_t headerTitleStyle;
lv_style_t buttonStyle;
lv_style_t buttonFocusedStyle;
lv_style_t listStyle;
lv_style_t listButtonStyle;
lv_style_t listButtonFocusedStyle;
lv_style_t checkedIndicatorStyle;
lv_style_t selectedItemStyle;
lv_style_t inputStyle;
lv_style_t inputFocusedStyle;
lv_style_t cursorStyle;
lv_style_t keyboardStyle;
lv_style_t keyboardKeyStyle;
lv_style_t keyboardKeyFocusedStyle;

// The look matches the Vanguard home screen: dark cards with thin teal borders and rounded corners,
// cyan for content, and green for whatever is selected.
void initStyles() {
    if (stylesInitialized) {
        return;
    }
    stylesInitialized = true;

    lv_style_init(&screenStyle);
    lv_style_set_bg_color(&screenStyle, background());
    lv_style_set_text_color(&screenStyle, text());

    // Containers/cards: the default theme makes these grey
    lv_style_init(&panelStyle);
    lv_style_set_bg_color(&panelStyle, surface());
    lv_style_set_border_color(&panelStyle, border());
    lv_style_set_radius(&panelStyle, RADIUS);

    // Keypad focus ring on anything focusable
    lv_style_init(&focusOutlineStyle);
    lv_style_set_outline_color(&focusOutlineStyle, green());

    // App title bars (Tactility toolbar), like the home screen header
    lv_style_init(&headerStyle);
    lv_style_set_bg_opa(&headerStyle, LV_OPA_COVER);
    lv_style_set_bg_color(&headerStyle, background());
    lv_style_set_border_side(&headerStyle, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&headerStyle, 1);
    lv_style_set_border_color(&headerStyle, border());
    lv_style_set_radius(&headerStyle, 0);

    lv_style_init(&headerTitleStyle);
    lv_style_set_text_color(&headerTitleStyle, accent());
    lv_style_set_text_letter_space(&headerTitleStyle, 1);

    // Buttons: cyan outline, solid green with dark text when selected. The background opacity is
    // set explicitly: some buttons (e.g. the toolbar close button) are transparent by default and
    // would otherwise show a dark icon on nothing.
    lv_style_init(&buttonStyle);
    lv_style_set_bg_color(&buttonStyle, surfaceRaised());
    lv_style_set_border_color(&buttonStyle, accentDim());
    lv_style_set_border_width(&buttonStyle, 1);
    lv_style_set_text_color(&buttonStyle, accent());
    lv_style_set_shadow_width(&buttonStyle, 0);
    lv_style_set_radius(&buttonStyle, 10);

    lv_style_init(&buttonFocusedStyle);
    lv_style_set_bg_opa(&buttonFocusedStyle, LV_OPA_COVER);
    lv_style_set_bg_color(&buttonFocusedStyle, green());
    lv_style_set_border_color(&buttonFocusedStyle, green());
    lv_style_set_text_color(&buttonFocusedStyle, textOnAccent());
    lv_style_set_outline_color(&buttonFocusedStyle, green());

    // Lists: rows are separate cards like the home screen tiles
    lv_style_init(&listStyle);
    lv_style_set_bg_color(&listStyle, background());
    lv_style_set_border_width(&listStyle, 0);
    lv_style_set_pad_all(&listStyle, 6);
    lv_style_set_pad_row(&listStyle, 4);

    lv_style_init(&listButtonStyle);
    lv_style_set_bg_opa(&listButtonStyle, LV_OPA_COVER);
    lv_style_set_bg_color(&listButtonStyle, surface());
    lv_style_set_text_color(&listButtonStyle, text());
    lv_style_set_border_side(&listButtonStyle, LV_BORDER_SIDE_FULL);
    lv_style_set_border_width(&listButtonStyle, 1);
    lv_style_set_border_color(&listButtonStyle, border());
    lv_style_set_radius(&listButtonStyle, 10);

    lv_style_init(&listButtonFocusedStyle);
    lv_style_set_bg_color(&listButtonFocusedStyle, selected());
    lv_style_set_text_color(&listButtonFocusedStyle, green());
    lv_style_set_border_color(&listButtonFocusedStyle, green());
    lv_style_set_border_width(&listButtonFocusedStyle, 2);
    lv_style_set_outline_width(&listButtonFocusedStyle, 0);

    // Switch on, checkbox ticked
    lv_style_init(&checkedIndicatorStyle);
    lv_style_set_bg_color(&checkedIndicatorStyle, green());
    lv_style_set_border_color(&checkedIndicatorStyle, green());

    // Selected option in dropdown lists and rollers
    lv_style_init(&selectedItemStyle);
    lv_style_set_bg_opa(&selectedItemStyle, LV_OPA_COVER);
    lv_style_set_bg_color(&selectedItemStyle, green());
    lv_style_set_text_color(&selectedItemStyle, textOnAccent());

    // Text fields and dropdowns
    lv_style_init(&inputStyle);
    lv_style_set_bg_color(&inputStyle, background());
    lv_style_set_border_color(&inputStyle, accentDim());
    lv_style_set_border_width(&inputStyle, 1);
    lv_style_set_text_color(&inputStyle, text());
    lv_style_set_radius(&inputStyle, 10);

    lv_style_init(&inputFocusedStyle);
    lv_style_set_border_color(&inputFocusedStyle, green());
    lv_style_set_outline_color(&inputFocusedStyle, green());

    lv_style_init(&cursorStyle);
    lv_style_set_border_color(&cursorStyle, green());

    // On-screen keyboard
    lv_style_init(&keyboardStyle);
    lv_style_set_bg_color(&keyboardStyle, background());
    lv_style_set_border_color(&keyboardStyle, border());

    lv_style_init(&keyboardKeyStyle);
    lv_style_set_bg_color(&keyboardKeyStyle, surfaceRaised());
    lv_style_set_text_color(&keyboardKeyStyle, text());
    lv_style_set_radius(&keyboardKeyStyle, 6);

    lv_style_init(&keyboardKeyFocusedStyle);
    lv_style_set_bg_color(&keyboardKeyFocusedStyle, green());
    lv_style_set_text_color(&keyboardKeyFocusedStyle, textOnAccent());
}

// LV_PART_* and LV_STATE_* are different enum types, which C++ doesn't allow combining directly
constexpr lv_style_selector_t selector(lv_part_t part, lv_state_t state) {
    return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
}

void addFocusStyle(lv_obj_t* obj, lv_style_t* style, lv_style_selector_t part) {
    lv_obj_add_style(obj, style, part | LV_STATE_FOCUSED);
    lv_obj_add_style(obj, style, part | LV_STATE_FOCUS_KEY);
    lv_obj_add_style(obj, style, part | LV_STATE_PRESSED);
}

// Tactility's toolbar is a private widget class: derived directly from lv_obj, without destructor
// or event handler (the status bar, the other such widget, has both).
bool isToolbar(lv_obj_t* obj) {
    if (obj == nullptr) {
        return false;
    }
    const lv_obj_class_t* object_class = lv_obj_get_class(obj);
    return object_class != &lv_obj_class &&
        object_class->base_class == &lv_obj_class &&
        object_class->destructor_cb == nullptr &&
        object_class->event_cb == nullptr &&
        object_class->instance_size > sizeof(lv_obj_t);
}

void applyStyles(lv_theme_t*, lv_obj_t* obj) {
    lv_obj_t* parent = lv_obj_get_parent(obj);
    if (parent == nullptr) {
        lv_obj_add_style(obj, &screenStyle, LV_PART_MAIN);
        return;
    }

    lv_obj_add_style(obj, &focusOutlineStyle, selector(LV_PART_MAIN, LV_STATE_FOCUS_KEY));

    if (isToolbar(obj)) {
        lv_obj_add_style(obj, &headerStyle, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_label_class) && isToolbar(lv_obj_get_parent(parent))) {
        lv_obj_add_style(obj, &headerTitleStyle, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_obj_class)) {
        lv_obj_add_style(obj, &panelStyle, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_list_class)) {
        lv_obj_add_style(obj, &listStyle, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_list_button_class)) {
        lv_obj_add_style(obj, &listButtonStyle, LV_PART_MAIN);
        addFocusStyle(obj, &listButtonFocusedStyle, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_add_style(obj, &buttonStyle, LV_PART_MAIN);
        addFocusStyle(obj, &buttonFocusedStyle, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_switch_class) || lv_obj_check_type(obj, &lv_checkbox_class)) {
        lv_obj_add_style(obj, &checkedIndicatorStyle, selector(LV_PART_INDICATOR, LV_STATE_CHECKED));
    } else if (lv_obj_check_type(obj, &lv_textarea_class) || lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_obj_add_style(obj, &inputStyle, LV_PART_MAIN);
        addFocusStyle(obj, &inputFocusedStyle, LV_PART_MAIN);
        if (lv_obj_check_type(obj, &lv_textarea_class)) {
            lv_obj_add_style(obj, &cursorStyle, selector(LV_PART_CURSOR, LV_STATE_FOCUSED));
        }
    } else if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_obj_add_style(obj, &inputStyle, LV_PART_MAIN);
        lv_obj_add_style(obj, &selectedItemStyle, selector(LV_PART_SELECTED, LV_STATE_CHECKED));
        lv_obj_add_style(obj, &selectedItemStyle, selector(LV_PART_SELECTED, LV_STATE_PRESSED));
    } else if (lv_obj_check_type(obj, &lv_roller_class)) {
        lv_obj_add_style(obj, &inputStyle, LV_PART_MAIN);
        lv_obj_add_style(obj, &selectedItemStyle, LV_PART_SELECTED);
    } else if (lv_obj_check_type(obj, &lv_keyboard_class)) {
        lv_obj_add_style(obj, &keyboardStyle, LV_PART_MAIN);
        lv_obj_add_style(obj, &keyboardKeyStyle, LV_PART_ITEMS);
        addFocusStyle(obj, &keyboardKeyFocusedStyle, LV_PART_ITEMS);
    }
}

lv_obj_tree_walk_res_t applyToExisting(lv_obj_t* obj, void*) {
    applyStyles(&vanguardTheme, obj);
    return LV_OBJ_TREE_WALK_NEXT;
}

void setTextColor(lv_obj_t* obj, lv_color_t color) {
    lv_obj_set_style_text_color(obj, color, LV_STATE_DEFAULT);
}

// The window manager builds: screen > root > vertical container > [status bar, app container].
// The status bar is a Tactility-internal widget (with white-on-black local styles), and the first
// widget along that path that isn't a plain container.
lv_obj_t* findStatusbar(lv_obj_t* screen) {
    lv_obj_t* obj = screen;
    for (int depth = 0; obj != nullptr && depth < 4; depth++) {
        obj = lv_obj_get_child(obj, 0);
        if (obj != nullptr && !lv_obj_check_type(obj, &lv_obj_class)) {
            return obj;
        }
    }
    return nullptr;
}

void styleStatusbar(lv_obj_t* screen) {
    lv_obj_t* statusbar = findStatusbar(screen);
    if (statusbar == nullptr) {
        LOG_W(TAG, "Status bar not found");
        return;
    }
    lv_obj_set_style_bg_color(statusbar, background(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(statusbar, LV_BORDER_SIDE_BOTTOM, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(statusbar, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(statusbar, accentDim(), LV_STATE_DEFAULT);
    // First child is the clock, the rest are spacers and status icons
    for (uint32_t i = 0; i < lv_obj_get_child_count(statusbar); i++) {
        setTextColor(lv_obj_get_child(statusbar, i), i == 0 ? green() : accent());
    }
}

} // namespace

void installSystemTheme(lv_display_t* display) {
    if (display == nullptr || lv_display_get_theme(display) == &vanguardTheme) {
        return;
    }

    initStyles();

    lv_theme_t* current = lv_display_get_theme(display);
    const lv_font_t* font = current != nullptr && current->font_normal != nullptr ? current->font_normal : LV_FONT_DEFAULT;
    lv_theme_t* base = lv_theme_default_init(display, accent(), green(), true, font);

    vanguardTheme = *base;
    lv_theme_set_parent(&vanguardTheme, base);
    lv_theme_set_apply_cb(&vanguardTheme, applyStyles);
    lv_display_set_theme(display, &vanguardTheme);

    // Widgets that already exist missed the theme's apply step
    lv_obj_t* roots[] = { lv_display_get_screen_active(display), lv_display_get_layer_top(display), lv_display_get_layer_sys(display) };
    for (auto* root : roots) {
        if (root != nullptr) {
            lv_obj_tree_walk(root, applyToExisting, nullptr);
        }
    }
    styleStatusbar(lv_display_get_screen_active(display));

    LOG_I(TAG, "Sysrupt theme installed");
}

}
