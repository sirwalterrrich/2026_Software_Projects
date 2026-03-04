#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>

#define NETWORK_STATUS_OK    0
#define NETWORK_STATUS_SLOW  1
#define NETWORK_STATUS_FAIL 2

typedef struct {
    int battery;           /* 0-100 */
    int ping_ms;
    int lifter_position;
    int estop_active;      /* 0 or 1 */
    int connection_state;  /* NETWORK_STATUS_* */
} RobotStatus;

void network_init(const char* robot_ip);
void network_send_control(float linear, float angular,
                          float cam_pan, float cam_tilt,
                          int lifter, int estop);
int network_fetch_status(RobotStatus* out);

/* Quick connection test to a specific IP (returns 1=OK, 0=fail) */
int network_test_connection(const char* ip);

/* Get milliseconds since last successful status fetch (0 if never connected) */
uint32_t network_get_last_ok_age_ms(void);

/* Send sound play command to robot (returns 1=OK, 0=fail) */
int network_play_sound(int sound_id);

/* Get WiFi RSSI signal strength (returns dBm, 0 if unavailable) */
int network_get_rssi(void);

#endif
