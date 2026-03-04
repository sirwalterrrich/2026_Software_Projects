#ifndef INPUT_H
#define INPUT_H

typedef struct {
    float linear;
    float angular;
    float cam_pan;
    float cam_tilt;
    int lifter;
    int estop;
    int switch_robot;
    int open_config;
    int screenshot;       /* Cross button pressed (edge) */
    int speed_change;     /* +1 = D-pad Up, -1 = D-pad Down (edge) */
    int switch_robot_lr;  /* +1 = D-pad Right, -1 = D-pad Left (edge, control mode only) */
    int select_pressed;   /* Select button pressed (edge) */
    int sound_screen;     /* Square button pressed (edge) */
} ControlState;

typedef struct {
    int x;
    int y;
    int active;
} TouchInput;

/* Speed preset levels */
#define SPEED_SLOW   0
#define SPEED_MEDIUM 1
#define SPEED_FAST   2
#define SPEED_COUNT  3

void input_init(void);
void input_update(ControlState* state);
TouchInput input_get_touch(void);
unsigned int input_get_buttons(void);
unsigned int input_get_buttons_pressed(void);

#endif
