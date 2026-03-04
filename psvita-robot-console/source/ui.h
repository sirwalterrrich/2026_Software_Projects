#ifndef UI_H
#define UI_H

#include "input.h"
#include "network.h"

/* UI modes */
#define UI_MODE_CONTROL 0
#define UI_MODE_CONFIG  1
#define UI_MODE_SOUNDS  2
#define UI_MODE_GALLERY 3
#define UI_MODE_HELP    4

void ui_init(void);
void ui_render(const ControlState* state, const RobotStatus* status, const char* robot_ip, int ui_mode);
void ui_cleanup(void);
int ui_robot_name_tapped(void);
void ui_draw_splash(const char* status_text);
void ui_save_screenshot(void);
void ui_check_screenshot(void);
void ui_toggle_log_viewer(void);
int ui_is_log_viewer_active(void);
void ui_set_speed_preset(int preset);
int ui_get_speed_preset(void);
float ui_get_speed_scale(void);
void ui_notify_config_saved(void);

#endif
