/*
 * Copyright (C) 2025 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/controls.h"

#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>

#define LEFT_ANALOG_DEADZONE  0.16f
#define RIGHT_ANALOG_DEADZONE 0.16f

extern void port_trace(const char *format, ...);

void coord_normalize(float * x, float * y, float deadzone) {
    float magnitude = sqrtf((*x * *x) + (*y * *y));
    if (magnitude < deadzone) {
        *x = 0;
        *y = 0;
        return;
    }

    // normalize
    *x = *x / magnitude;
    *y = *y / magnitude;

    float multiplier = ((magnitude - deadzone) / (1 - deadzone));
    *x = *x * multiplier;
    *y = *y * multiplier;
}

void controls_init() {
    // Enable analog sticks and touchscreen
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    int touch_result = sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);
    port_trace("Touch init: front sampling result=0x%x", touch_result);

    // Enable accelerometer
    sceMotionStartSampling();
}

void poll_touch();
void poll_pad();
void poll_accel();

void poll_stick(ControlsStickId which, float raw_x, float raw_y, float * readings_x, float * readings_y, float deadzone);

void controls_poll() {
    poll_touch();
    poll_pad();
    //poll_accel();
}

SceTouchData touch;
static int32_t active_touch_id = -1;
static float active_touch_x;
static float active_touch_y;

static void touch_to_game_coords(const SceTouchReport *report,
                                 float *x, float *y) {
    *x = (float)report->x * 960.0f / 1920.0f;
    *y = (float)report->y * 544.0f / 1088.0f;
    if (*x < 0.0f) *x = 0.0f;
    if (*y < 0.0f) *y = 0.0f;
    if (*x > 959.0f) *x = 959.0f;
    if (*y > 543.0f) *y = 543.0f;
}

void poll_touch() {
    sceClibMemset(&touch, 0, sizeof(touch));
    int read = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    if (read < 0)
        return;

    const SceTouchReport *current = NULL;
    if (active_touch_id >= 0) {
        for (int i = 0; i < touch.reportNum; ++i) {
            if ((int32_t)touch.report[i].id == active_touch_id) {
                current = &touch.report[i];
                break;
            }
        }
    }

    if (active_touch_id >= 0 && !current) {
        controls_handler_touch(active_touch_id, active_touch_x,
                               active_touch_y, CONTROLS_ACTION_UP);
        active_touch_id = -1;
    }

    if (active_touch_id < 0 && touch.reportNum > 0) {
        current = &touch.report[0];
        active_touch_id = (int32_t)current->id;
        touch_to_game_coords(current, &active_touch_x, &active_touch_y);
        static unsigned traced_down;
        if (traced_down < 4)
            port_trace("Touch map DOWN #%u vita_id=%d android_id=0 raw=%u,%u mapped=%.1f,%.1f",
                       ++traced_down, active_touch_id, current->x, current->y,
                       active_touch_x, active_touch_y);
        controls_handler_touch(active_touch_id, active_touch_x,
                               active_touch_y, CONTROLS_ACTION_DOWN);
        return;
    }

    if (current) {
        float x, y;
        touch_to_game_coords(current, &x, &y);
        if (fabsf(x - active_touch_x) >= 0.5f ||
            fabsf(y - active_touch_y) >= 0.5f) {
            active_touch_x = x;
            active_touch_y = y;
            controls_handler_touch(active_touch_id, active_touch_x,
                                   active_touch_y, CONTROLS_ACTION_MOVE);
        }
    }
}

static ButtonMapping mapping[] = {
        { SCE_CTRL_UP,        AKEYCODE_DPAD_UP },
        { SCE_CTRL_DOWN,      AKEYCODE_DPAD_DOWN },
        { SCE_CTRL_LEFT,      AKEYCODE_DPAD_LEFT },
        { SCE_CTRL_RIGHT,     AKEYCODE_DPAD_RIGHT },
        { SCE_CTRL_CROSS,     AKEYCODE_BUTTON_A },
        { SCE_CTRL_CIRCLE,    AKEYCODE_BUTTON_B },
        { SCE_CTRL_SQUARE,    AKEYCODE_BUTTON_X },
        { SCE_CTRL_TRIANGLE,  AKEYCODE_BUTTON_Y },
        { SCE_CTRL_L1,        AKEYCODE_BUTTON_L1 },
        { SCE_CTRL_R1,        AKEYCODE_BUTTON_R1 },
        { SCE_CTRL_START,     AKEYCODE_BUTTON_START },
        { SCE_CTRL_SELECT,    AKEYCODE_BUTTON_SELECT },
};

uint32_t old_buttons = 0, current_buttons = 0, pressed_buttons = 0, released_buttons = 0;

float analog_lx[3] = { 0 };
float analog_ly[3] = { 0 };
float analog_rx[3] = { 0 };
float analog_ry[3] = { 0 };

void poll_pad() {
    SceCtrlData pad;
    sceCtrlPeekBufferPositiveExt2(0, &pad, 1);

    // Gamepad buttons
    old_buttons = current_buttons;
    current_buttons = pad.buttons;
    pressed_buttons = current_buttons & ~old_buttons;
    released_buttons = ~current_buttons & old_buttons;

    for (int i = 0; i < sizeof(mapping) / sizeof(ButtonMapping); i++) {
        if (pressed_buttons & mapping[i].sce_button) {
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_DOWN);
        }
        if (released_buttons & mapping[i].sce_button) {
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_UP);
        }
    }

    // Analog sticks
    poll_stick(CONTROLS_STICK_LEFT, (float)pad.lx, (float)pad.ly, analog_lx, analog_ly, LEFT_ANALOG_DEADZONE);
    poll_stick(CONTROLS_STICK_RIGHT, (float)pad.rx, (float)pad.ry, analog_rx, analog_ry, RIGHT_ANALOG_DEADZONE);
}

void poll_stick(ControlsStickId which, float raw_x, float raw_y,
                float *readings_x, float *readings_y, float deadzone) {
    float previous_x = readings_x[0];
    float previous_y = readings_y[0];
    float current_x = (raw_x - 128.0f) / 128.0f;
    float current_y = (raw_y - 128.0f) / 128.0f;

    coord_normalize(&current_x, &current_y, deadzone);

    int was_neutral = previous_x == 0.0f && previous_y == 0.0f;
    int is_neutral = current_x == 0.0f && current_y == 0.0f;

    if (!was_neutral && is_neutral) {
        controls_handler_analog(which, 0.0f, 0.0f, CONTROLS_ACTION_UP);
    } else if (was_neutral && !is_neutral) {
        controls_handler_analog(which, current_x, current_y,
                                CONTROLS_ACTION_DOWN);
    } else if (!is_neutral &&
               (fabsf(current_x - previous_x) >= 0.01f ||
                fabsf(current_y - previous_y) >= 0.01f)) {
        controls_handler_analog(which, current_x, current_y,
                                CONTROLS_ACTION_MOVE);
    }

    readings_x[2] = readings_x[1];
    readings_y[2] = readings_y[1];
    readings_x[1] = previous_x;
    readings_y[1] = previous_y;
    readings_x[0] = current_x;
    readings_y[0] = current_y;
}
