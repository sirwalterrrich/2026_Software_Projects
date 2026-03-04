#ifndef ROBOT_H
#define ROBOT_H

#define ROBOT_MAX 6

int robot_config_load(void);
void robot_init(void);
void robot_switch(int direction);
const char* robot_get_active_ip(void);
int robot_get_count(void);
int robot_get_active_index(void);
const char* robot_get_ip_by_index(int idx);
void robot_set_ip_by_index(int idx, const char* ip);
const char* robot_get_camera_host(int idx);
void robot_set_camera_host(int idx, const char* host);
int robot_get_camera_port(int idx);
void robot_set_camera_port(int idx, int port);
const char* robot_get_stream_endpoint(int idx);
void robot_set_stream_endpoint(int idx, const char* endpoint);
int robot_save_config(void);

/* Add a new robot with default settings (returns new index, or -1 if full) */
int robot_add(void);

/* Remove robot at index (returns 0=OK, -1=fail, won't remove last robot) */
int robot_remove(int idx);

/* Per-robot speed preset */
void robot_set_speed_preset(int idx, int preset);
int robot_get_speed_preset(int idx);

/* Global UI sounds mute setting (persisted in robots.json) */
void robot_set_ui_sounds_muted(int muted);
int robot_get_ui_sounds_muted(void);

#endif