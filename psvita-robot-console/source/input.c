#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <math.h>
#include <stdio.h>
#include "input.h"

#define DEADZONE 24

static int prev_triangle;
static TouchInput touch_state = {0, 0, 0};
static unsigned int cur_buttons = 0;
static unsigned int prev_buttons = 0;

/* Rear touchpad state for camera control */
static float rear_cam_pan = 0.0f;
static float rear_cam_tilt = 0.0f;

static float clamp_axis(int raw) {
    int v = raw - 128;
    if (v > -DEADZONE && v < DEADZONE) return 0.0f;
    if (v > 0) v -= DEADZONE;
    else v += DEADZONE;
    float n = (float)v / (128.0f - DEADZONE);
    if (n > 1.0f) n = 1.0f;
    if (n < -1.0f) n = -1.0f;
    return n;
}

void input_init(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, 1);
}

void input_update(ControlState* state) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);

    prev_buttons = cur_buttons;
    cur_buttons = pad.buttons;

    state->linear  = -clamp_axis(pad.ly);
    state->angular = clamp_axis(pad.lx);

    /* Camera: right stick + rear touchpad overlay */
    float stick_pan  = clamp_axis(pad.rx);
    float stick_tilt = -clamp_axis(pad.ry);

    /* Read rear touchpad for camera */
    SceTouchData back_touch;
    sceTouchPeek(SCE_TOUCH_PORT_BACK, &back_touch, 1);
    if (back_touch.reportNum > 0) {
        /* Rear touch: 1920x890 mapped to -1..1 */
        rear_cam_pan  = ((float)back_touch.report[0].x / 960.0f) - 1.0f;
        rear_cam_tilt = -(((float)back_touch.report[0].y / 445.0f) - 1.0f);
    } else {
        rear_cam_pan = 0.0f;
        rear_cam_tilt = 0.0f;
    }

    /* Combine: stick takes priority, rear touch adds when stick is idle */
    state->cam_pan  = (stick_pan  != 0.0f) ? stick_pan  : rear_cam_pan;
    state->cam_tilt = (stick_tilt != 0.0f) ? stick_tilt : rear_cam_tilt;

    state->lifter = 0;
    if (pad.buttons & SCE_CTRL_LTRIGGER) state->lifter = -1;
    if (pad.buttons & SCE_CTRL_RTRIGGER) state->lifter = 1;

    /* E-Stop: latching toggle on Circle press */
    {
        static int estop_latched = 0;
        int circle_now = (pad.buttons & SCE_CTRL_CIRCLE) ? 1 : 0;
        static int prev_circle = 0;
        if (circle_now && !prev_circle)
            estop_latched = !estop_latched;
        prev_circle = circle_now;
        state->estop = estop_latched;
    }

    state->switch_robot = 0;
    if ((pad.buttons & SCE_CTRL_TRIANGLE) && !prev_triangle)
        state->switch_robot = 1;
    prev_triangle = (pad.buttons & SCE_CTRL_TRIANGLE) ? 1 : 0;

    state->open_config = (cur_buttons & SCE_CTRL_START) && !(prev_buttons & SCE_CTRL_START);

    /* Cross button: screenshot (edge-triggered) */
    state->screenshot = (cur_buttons & SCE_CTRL_CROSS) && !(prev_buttons & SCE_CTRL_CROSS);

    /* D-Pad Up/Down: speed preset change (edge-triggered) */
    state->speed_change = 0;
    if ((cur_buttons & SCE_CTRL_UP) && !(prev_buttons & SCE_CTRL_UP))
        state->speed_change = 1;
    if ((cur_buttons & SCE_CTRL_DOWN) && !(prev_buttons & SCE_CTRL_DOWN))
        state->speed_change = -1;

    /* D-Pad Left/Right: robot switch (edge-triggered, used in control mode) */
    state->switch_robot_lr = 0;
    if ((cur_buttons & SCE_CTRL_RIGHT) && !(prev_buttons & SCE_CTRL_RIGHT))
        state->switch_robot_lr = 1;
    if ((cur_buttons & SCE_CTRL_LEFT) && !(prev_buttons & SCE_CTRL_LEFT))
        state->switch_robot_lr = -1;

    /* Select button (edge-triggered) */
    state->select_pressed = (cur_buttons & SCE_CTRL_SELECT) && !(prev_buttons & SCE_CTRL_SELECT);

    /* Square button: sound screen (edge-triggered) */
    state->sound_screen = (cur_buttons & SCE_CTRL_SQUARE) && !(prev_buttons & SCE_CTRL_SQUARE);

    /* Read front touch input */
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    if (touch.reportNum > 0) {
        touch_state.x = touch.report[0].x / 2;
        touch_state.y = touch.report[0].y / 2;
        touch_state.active = 1;
    } else {
        touch_state.active = 0;
    }
}

TouchInput input_get_touch(void) {
    return touch_state;
}

unsigned int input_get_buttons(void) {
    return cur_buttons;
}

unsigned int input_get_buttons_pressed(void) {
    return cur_buttons & ~prev_buttons;
}
