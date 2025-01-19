/* Copyright 2023 Colin Lam (Ploopy Corporation)
 * Copyright 2020 Christopher Courtney, aka Drashna Jael're  (@drashna) <drashna@live.com>
 * Copyright 2019 Sunjun Kim
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H

float divisor_array[] = PLOOPY_DIVISOR_OPTIONS;
#define DIVISOR_OPTION_SIZE ARRAY_SIZE(divisor_array)

/**
 * Record types
 */
// clang-format off
typedef enum {
    NON_TAP_RELEASE = 0b00,
    NON_TAP_PRESS = 0b01,
    TAP_RELEASE = 0b10,
    TAP_PRESS = 0b11
} keyrecord_type_t;
// clang-format on

/**
 * User state
 */
typedef struct {
    uint16_t click_burst_count;     // clicks scheduled by a burst
    uint16_t click_burst_remaining; // remaining clicks. Can't exceed (2 * autoclick_burst_count)
    uint16_t click_burst_delay;     // minimum time (ms) between each click
    uint16_t click_burst_timer;
    float    scroll_accumulated_h;
    float    scroll_accumulated_v;
    float    drag_scroll_divisor;
    bool     is_drag_scroll;
    union persistent_user_state_t {
        uint32_t raw;
        struct {
            uint8_t divisor_config;
        };
    } persistent;
} ploopy_user_state_t;
ploopy_user_state_t user_state;

void td_bc_inc_on_tap(tap_dance_state_t *state, void *user_data);
void td_bc_inc_on_finished(tap_dance_state_t *state, void *user_data);

void td_bc_dec_on_tap(tap_dance_state_t *state, void *user_data);
void td_bc_dec_on_finished(tap_dance_state_t *state, void *user_data);

void td_bd_inc_on_tap(tap_dance_state_t *state, void *user_data);
void td_bd_inc_on_finished(tap_dance_state_t *state, void *user_data);

void td_bd_dec_on_tap(tap_dance_state_t *state, void *user_data);
void td_bd_dec_on_finished(tap_dance_state_t *state, void *user_data);

void td_cd_res_on_tap(tap_dance_state_t *state, void *user_data);
void td_cd_res_on_finished(tap_dance_state_t *state, void *user_data);

keyrecord_type_t get_keyrecord_type_with_tap(keyrecord_t *);

void toggle_drag_scroll_user(void);
void set_drag_scroll_user(bool);
void cycle_divisor(void);
void do_burst_step(void);

void enqueue_burst(void);
void halt_burst(void);

bool process_record_user_oe_drag(keyrecord_t *);

void persist_state_without_check(void);
void persist_state(void);

enum aliases {
    KC_OE_DRAG = LT(0, KC_NO), // Over-engineered drag scroll button. Toggle DS on tap, Layer switch and momentary drag scroll on hold.
};

enum user_keys {
    DIVISOR_CONFIG = QK_USER,
    DRAG_TOGGLE,
};

enum combos { CMB_M1_BURST };

enum tap_dances {
    TD_BC_INC, // Tap dance burst count increment (1/5)
    TD_BC_DEC, // Tap dance burst count decrement (1/5)
    TD_BD_INC, // Tap dance burst delay increment (10/50)
    TD_BD_DEC, // Tap dance burst delay decrement (10/50)
    TD_CD_RES, // Tap dance burst count/delay reset
};

enum layers {
    BASE,
    LIMBO, // Jumps to other layers
    DPI,
    BURST
};

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [BASE ] = LAYOUT(KC_BTN4      , KC_BTN5       , KC_OE_DRAG, KC_BTN2      , KC_BTN1      , KC_BTN3      ),
    [LIMBO] = LAYOUT(TO(DPI)      , TO(BURST)     , _______   , _______      , _______      , _______      ),
    [DPI  ] = LAYOUT(DPI_CONFIG   , DIVISOR_CONFIG, TO(BASE)  , DRAG_TOGGLE  , _______      , _______      ),
    [BURST] = LAYOUT(TD(TD_BC_INC), TD(TD_CD_RES) , TO(BASE)  , TD(TD_BD_INC), TD(TD_BC_DEC), TD(TD_BD_DEC)),
};

const uint16_t PROGMEM multitap_combo[] = {KC_BTN1, KC_BTN3, COMBO_END};

combo_t key_combos[] = {
    [CMB_M1_BURST] = COMBO(multitap_combo, KC_NO)
};

tap_dance_action_t tap_dance_actions[] = {
    [TD_BC_INC] = ACTION_TAP_DANCE_FN_ADVANCED(td_bc_inc_on_tap, td_bc_inc_on_finished, NULL),
    [TD_BC_DEC] = ACTION_TAP_DANCE_FN_ADVANCED(td_bc_dec_on_tap, td_bc_dec_on_finished, NULL),
    [TD_BD_INC] = ACTION_TAP_DANCE_FN_ADVANCED(td_bd_inc_on_tap, td_bd_inc_on_finished, NULL),
    [TD_BD_DEC] = ACTION_TAP_DANCE_FN_ADVANCED(td_bd_dec_on_tap, td_bd_dec_on_finished, NULL),
    [TD_CD_RES] = ACTION_TAP_DANCE_FN_ADVANCED(td_cd_res_on_tap, td_cd_res_on_finished, NULL)
};
// clang-format on

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case KC_OE_DRAG:
            return process_record_user_oe_drag(record);
        case DIVISOR_CONFIG:
            if (record->event.pressed) {
                cycle_divisor();
            }
            return false;
        case DRAG_TOGGLE:
            if (record->event.pressed) {
                toggle_drag_scroll_user();
            }
            return false;
        default:
            return true;
    }
}

void matrix_scan_user(void) {
    if (user_state.click_burst_remaining && timer_elapsed(user_state.click_burst_timer) > user_state.click_burst_delay) {
        do_burst_step();
    }
}

bool process_combo_key_repress(uint16_t combo_index, combo_t *combo, uint8_t key_index, uint16_t keycode) {
    switch (combo_index) {
        case CMB_M1_BURST:
            switch (keycode) {
                case KC_BTN1:
                    enqueue_burst();
                    return true;
                case KC_BTN3:
                    halt_burst();
                    return true;
            }
    }
    return false;
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    if (user_state.is_drag_scroll) {
        user_state.scroll_accumulated_h += (float)mouse_report.x / user_state.drag_scroll_divisor;
        user_state.scroll_accumulated_v += (float)mouse_report.y / user_state.drag_scroll_divisor;

        // Assign integer parts of accumulated scroll values to the mouse report
        mouse_report.h = (int8_t)user_state.scroll_accumulated_h;
        mouse_report.v = -(int8_t)user_state.scroll_accumulated_v;
        ;

        // Update accumulated scroll values by subtracting the integer parts
        user_state.scroll_accumulated_h -= (int8_t)user_state.scroll_accumulated_h;
        user_state.scroll_accumulated_v -= (int8_t)user_state.scroll_accumulated_v;

        // Clear the X and Y values of the mouse report
        mouse_report.x = 0;
        mouse_report.y = 0;

        mouse_report.x = 0;
        mouse_report.y = 0;
    }
    return mouse_report;
}

/**
 * Tap dance functions
 */
void td_bc_inc_on_tap(tap_dance_state_t *state, void *user_data) {
    if (state->count == 2) {
        user_state.click_burst_count = MIN(user_state.click_burst_count + BURST_COUNT_CHANGE_LARGE, BURST_COUNT_MAX);
        state->finished              = true;
    }
}

void td_bc_inc_on_finished(tap_dance_state_t *state, void *user_data) {
    user_state.click_burst_count = MIN(user_state.click_burst_count + BURST_COUNT_CHANGE_SMALL, BURST_COUNT_MAX);
}

void td_bc_dec_on_tap(tap_dance_state_t *state, void *user_data) {
    if (state->count == 2) {
        // Account for underflows
        if (user_state.click_burst_count > BURST_COUNT_CHANGE_LARGE) {
            user_state.click_burst_count = MAX(user_state.click_burst_count - BURST_COUNT_CHANGE_LARGE, BURST_COUNT_MIN);
        }
        state->finished = true;
    }
}

void td_bc_dec_on_finished(tap_dance_state_t *state, void *user_data) {
    user_state.click_burst_count = MAX(user_state.click_burst_count - BURST_COUNT_CHANGE_SMALL, BURST_COUNT_MIN);
}

void td_bd_inc_on_tap(tap_dance_state_t *state, void *user_data) {
    if (state->count == 2) {
        user_state.click_burst_delay = MIN(user_state.click_burst_delay + BURST_DELAY_CHANGE_LARGE, BURST_DELAY_MAX);
        state->finished              = true;
    }
}

void td_bd_inc_on_finished(tap_dance_state_t *state, void *user_data) {
    user_state.click_burst_delay = MIN(user_state.click_burst_delay + BURST_DELAY_CHANGE_SMALL, BURST_DELAY_MAX);
}

void td_bd_dec_on_tap(tap_dance_state_t *state, void *user_data) {
    if (state->count == 2) {
        // Account for underflows
        if (user_state.click_burst_delay > BURST_DELAY_CHANGE_LARGE) {
            user_state.click_burst_delay = MAX(user_state.click_burst_delay - BURST_DELAY_CHANGE_LARGE, BURST_DELAY_MIN);
            state->finished              = true;
        }
    }
}

void td_bd_dec_on_finished(tap_dance_state_t *state, void *user_data) {
    user_state.click_burst_delay = MAX(user_state.click_burst_delay - BURST_DELAY_CHANGE_SMALL, BURST_DELAY_MIN);
}

void td_cd_res_on_tap(tap_dance_state_t *state, void *user_data) {
    if (state->count == 2) {
        user_state.click_burst_count = BURST_COUNT_DEFAULT;
        state->finished              = true;
    }
}

void td_cd_res_on_finished(tap_dance_state_t *state, void *user_data) {
    user_state.click_burst_delay = BURST_DELAY_DEFAULT;
}

/**
 * Record processors
 */
bool process_record_user_oe_drag(keyrecord_t *record) {
    switch (get_keyrecord_type_with_tap(record)) {
        case TAP_PRESS:
            toggle_drag_scroll_user();
            break;
        case TAP_RELEASE:
            break;
        case NON_TAP_PRESS:
            set_drag_scroll_user(true);
            layer_move(LIMBO);
            break;
        case NON_TAP_RELEASE:
            set_drag_scroll_user(false);
            layer_off(LIMBO);
            break;
    }
    return false;
}

/* Utils */
keyrecord_type_t get_keyrecord_type_with_tap(keyrecord_t *record) {
    bool is_tap   = record->tap.count;
    bool is_press = record->event.pressed;
    dprintf("Record type: [%d, %d]\n", is_tap, is_press);
    return (is_tap << 1) | is_press;
}

void toggle_drag_scroll_user(void) {
    user_state.is_drag_scroll ^= 1;
}

void set_drag_scroll_user(bool b) {
    user_state.is_drag_scroll = b;
}

void cycle_divisor(void) {
    user_state.persistent.divisor_config = (user_state.persistent.divisor_config + 1) % DIVISOR_OPTION_SIZE;
    persist_state_without_check();
    user_state.drag_scroll_divisor = divisor_array[user_state.persistent.divisor_config];
}

void do_burst_step(void) {
    user_state.click_burst_timer = timer_read();
    tap_code(KC_BTN1);
    user_state.click_burst_remaining--;
}

void enqueue_burst(void) {
    // Set the timer to now in the end if there is no active burst
    bool no_active_burst = (user_state.click_burst_remaining == 0);

    // Allow at most two bursts (one active, one in queue)
    if (user_state.click_burst_remaining <= user_state.click_burst_count) {
        user_state.click_burst_remaining += user_state.click_burst_count;
    }

    if (no_active_burst) {
        do_burst_step();
    }
}

void halt_burst(void) {
    user_state.click_burst_remaining = 0;
}

/* Device setup */
void keyboard_post_init_user(void) {
    debug_enable   = false;
    debug_keyboard = false;
    debug_mouse    = false;
}

void pointing_device_init_user() {
    user_state.scroll_accumulated_h  = 0;
    user_state.scroll_accumulated_v  = 0;
    user_state.is_drag_scroll        = false;
    user_state.click_burst_remaining = 0;
    user_state.click_burst_count     = BURST_COUNT_DEFAULT;
    user_state.click_burst_delay     = BURST_DELAY_DEFAULT;
    user_state.click_burst_remaining = 0;

    eeconfig_init_user();
    user_state.drag_scroll_divisor = divisor_array[user_state.persistent.divisor_config];
}

void eeconfig_init_user(void) {
    user_state.persistent.raw = eeconfig_read_user();
    // Do this check to avoid unnecessary writes.
    if (user_state.persistent.divisor_config > DIVISOR_OPTION_SIZE) {
        user_state.persistent.divisor_config = PLOOPY_DIVISOR_DEFAULT;
        persist_state_without_check();
    }
}

void persist_state(void) {
    union persistent_user_state_t previous_persistent_state = {.raw = eeconfig_read_user()};
    if (user_state.persistent.raw != previous_persistent_state.raw) {
        persist_state_without_check();
    }
}

void persist_state_without_check(void) {
    eeconfig_update_user(user_state.persistent.raw);
}
