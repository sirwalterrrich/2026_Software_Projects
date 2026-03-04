#include "robot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_IP_LEN 64
#define CONFIG_PATHS_COUNT 4

typedef struct {
    char ips[ROBOT_MAX][MAX_IP_LEN];
    char camera_hosts[ROBOT_MAX][MAX_IP_LEN];  /* Empty = use robot IP */
    int camera_ports[ROBOT_MAX];
    char stream_endpoints[ROBOT_MAX][64];
    int speed_presets[ROBOT_MAX];  /* 0=SLOW, 1=MED, 2=FAST */
    int count;
    int ui_sounds_muted;  /* Global: 0=on, 1=muted */
} RobotConfig;

static RobotConfig robot_cfg = {0};
static int active_robot = 0;

/* Try multiple paths for config file (app0:, ux0:, relative, etc.) */
static const char* const CONFIG_PATHS[] = {
    "app0:/robots.json",
    "ux0:/data/RobotConsole/robots.json",
    "robots.json",
    "assets/robots.json"
};

static void set_robot_defaults(int idx) {
    robot_cfg.camera_hosts[idx][0] = '\0';  /* Empty = use robot IP */
    robot_cfg.camera_ports[idx] = 5000;
    strcpy(robot_cfg.stream_endpoints[idx], "/stream");
    robot_cfg.speed_presets[idx] = 2;  /* FAST by default */
}

/* Extract a quoted string value for a key within a bounded region */
static int extract_string_field(const char* start, const char* end, const char* key, char* out, int out_size) {
    char search[80];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(start, search);
    if (!p || p >= end) return -1;
    p = strchr(p, ':');
    if (!p || p >= end) return -1;
    p = strchr(p, '"');
    if (!p || p >= end) return -1;
    p++;
    const char* q = strchr(p, '"');
    if (!q || q >= end) return -1;
    int len = q - p;
    if (len >= out_size) len = out_size - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* Extract an integer value for a key within a bounded region */
static int extract_int_field(const char* start, const char* end, const char* key, int default_val) {
    char search[80];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(start, search);
    if (!p || p >= end) return default_val;
    p = strchr(p, ':');
    if (!p || p >= end) return default_val;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    int val = 0;
    if (sscanf(p, "%d", &val) == 1) return val;
    return default_val;
}

static int parse_json_config(const char* json_str) {
    if (!json_str) return -1;

    int robot_idx = 0;
    const char* ptr = json_str;

    /* Find "robots":[...] array */
    ptr = strstr(ptr, "\"robots\"");
    if (!ptr) return -1;

    ptr = strchr(ptr, '[');
    if (!ptr) return -1;
    ptr++; /* skip '[' */

    /* Find matching ']' */
    const char* array_end = strchr(ptr, ']');
    if (!array_end) return -1;

    /* Parse each element -- could be a string "ip" or an object { ... } */
    while (*ptr && ptr < array_end && robot_idx < ROBOT_MAX) {
        /* Skip whitespace and commas */
        while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r' || *ptr == ',') ptr++;
        if (ptr >= array_end) break;

        set_robot_defaults(robot_idx);

        if (*ptr == '{') {
            /* Object format: { "ip": "...", "camera_port": N, "stream_endpoint": "..." } */
            const char* obj_start = ptr;
            const char* obj_end = strchr(ptr, '}');
            if (!obj_end) break;
            obj_end++; /* include '}' */

            extract_string_field(obj_start, obj_end, "ip",
                                 robot_cfg.ips[robot_idx], MAX_IP_LEN);
            extract_string_field(obj_start, obj_end, "camera_host",
                                 robot_cfg.camera_hosts[robot_idx], MAX_IP_LEN);
            robot_cfg.camera_ports[robot_idx] =
                extract_int_field(obj_start, obj_end, "camera_port", 5000);
            extract_string_field(obj_start, obj_end, "stream_endpoint",
                                 robot_cfg.stream_endpoints[robot_idx], 64);
            robot_cfg.speed_presets[robot_idx] =
                extract_int_field(obj_start, obj_end, "speed_preset", 2);

            robot_idx++;
            ptr = obj_end;
        } else if (*ptr == '"') {
            /* Old string format: "10.10.1.247" */
            ptr++; /* skip opening quote */
            const char* end = strchr(ptr, '"');
            if (!end) break;
            int len = end - ptr;
            if (len > 0 && len < MAX_IP_LEN) {
                strncpy(robot_cfg.ips[robot_idx], ptr, len);
                robot_cfg.ips[robot_idx][len] = '\0';
                robot_idx++;
            }
            ptr = end + 1;
        } else {
            ptr++;
        }
    }

    robot_cfg.count = robot_idx;

    /* Parse global settings (outside robots array) */
    robot_cfg.ui_sounds_muted = extract_int_field(json_str, json_str + strlen(json_str),
                                                   "ui_sounds_muted", 0);

    return robot_idx > 0 ? 0 : -1;
}

int robot_config_load(void) {
    /* Try each config path */
    for (int i = 0; i < CONFIG_PATHS_COUNT; i++) {
        FILE* f = fopen(CONFIG_PATHS[i], "r");
        if (!f) continue;
        
        /* Read file into buffer */
        char buf[1024] = {0};
        size_t bytes_read = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        
        if (bytes_read > 0) {
            buf[bytes_read] = '\0';
            if (parse_json_config(buf) == 0) {
                return 0;  /* Success */
            }
        }
    }
    
    /* Fallback: hardcoded defaults */
    strcpy(robot_cfg.ips[0], "192.168.1.100");
    strcpy(robot_cfg.ips[1], "192.168.1.101");
    strcpy(robot_cfg.ips[2], "192.168.1.102");
    for (int i = 0; i < 3; i++) set_robot_defaults(i);
    robot_cfg.count = 3;

    return -1;  /* Using fallback defaults */
}

void robot_init(void) {
    robot_config_load();
    active_robot = 0;
}

void robot_switch(int direction) {
    if (robot_cfg.count <= 0) return;
    
    active_robot += direction;
    
    /* Wrap around */
    if (active_robot >= robot_cfg.count)
        active_robot = 0;
    else if (active_robot < 0)
        active_robot = robot_cfg.count - 1;
}

const char* robot_get_active_ip(void) {
    if (active_robot < 0 || active_robot >= robot_cfg.count) 
        return "192.168.1.100";  /* Fallback */
    return robot_cfg.ips[active_robot];
}

int robot_get_count(void) {
    return robot_cfg.count;
}

int robot_get_active_index(void) {
    return active_robot;
}
const char* robot_get_ip_by_index(int idx) {
    if (idx < 0 || idx >= robot_cfg.count)
        return "192.168.1.100";
    return robot_cfg.ips[idx];
}

void robot_set_ip_by_index(int idx, const char* ip) {
    if (idx < 0 || idx >= robot_cfg.count || !ip) return;
    strncpy(robot_cfg.ips[idx], ip, MAX_IP_LEN - 1);
    robot_cfg.ips[idx][MAX_IP_LEN - 1] = '\0';
}

const char* robot_get_camera_host(int idx) {
    if (idx < 0 || idx >= robot_cfg.count)
        return "";
    return robot_cfg.camera_hosts[idx];
}

void robot_set_camera_host(int idx, const char* host) {
    if (idx < 0 || idx >= robot_cfg.count || !host) return;
    strncpy(robot_cfg.camera_hosts[idx], host, MAX_IP_LEN - 1);
    robot_cfg.camera_hosts[idx][MAX_IP_LEN - 1] = '\0';
}

int robot_get_camera_port(int idx) {
    if (idx < 0 || idx >= robot_cfg.count)
        return 5000;
    return robot_cfg.camera_ports[idx];
}

void robot_set_camera_port(int idx, int port) {
    if (idx < 0 || idx >= robot_cfg.count) return;
    robot_cfg.camera_ports[idx] = port;
}

const char* robot_get_stream_endpoint(int idx) {
    if (idx < 0 || idx >= robot_cfg.count)
        return "/stream";
    return robot_cfg.stream_endpoints[idx];
}

void robot_set_stream_endpoint(int idx, const char* endpoint) {
    if (idx < 0 || idx >= robot_cfg.count || !endpoint) return;
    strncpy(robot_cfg.stream_endpoints[idx], endpoint, 63);
    robot_cfg.stream_endpoints[idx][63] = '\0';
}

int robot_add(void) {
    if (robot_cfg.count >= ROBOT_MAX) return -1;
    int idx = robot_cfg.count;
    snprintf(robot_cfg.ips[idx], MAX_IP_LEN, "192.168.1.%d", 100 + idx);
    set_robot_defaults(idx);
    robot_cfg.count++;
    return idx;
}

int robot_remove(int idx) {
    if (robot_cfg.count <= 1) return -1;  /* Keep at least 1 robot */
    if (idx < 0 || idx >= robot_cfg.count) return -1;

    /* Shift remaining robots down */
    for (int i = idx; i < robot_cfg.count - 1; i++) {
        strncpy(robot_cfg.ips[i], robot_cfg.ips[i + 1], MAX_IP_LEN);
        strncpy(robot_cfg.camera_hosts[i], robot_cfg.camera_hosts[i + 1], MAX_IP_LEN);
        robot_cfg.camera_ports[i] = robot_cfg.camera_ports[i + 1];
        strncpy(robot_cfg.stream_endpoints[i], robot_cfg.stream_endpoints[i + 1], 64);
        robot_cfg.speed_presets[i] = robot_cfg.speed_presets[i + 1];
    }
    robot_cfg.count--;

    /* Adjust active robot index */
    if (active_robot >= robot_cfg.count)
        active_robot = robot_cfg.count - 1;

    return 0;
}

void robot_set_speed_preset(int idx, int preset) {
    if (idx < 0 || idx >= robot_cfg.count) return;
    if (preset < 0) preset = 0;
    if (preset > 2) preset = 2;
    robot_cfg.speed_presets[idx] = preset;
}

int robot_get_speed_preset(int idx) {
    if (idx < 0 || idx >= robot_cfg.count) return 2;
    return robot_cfg.speed_presets[idx];
}

void robot_set_ui_sounds_muted(int muted) {
    robot_cfg.ui_sounds_muted = muted ? 1 : 0;
}

int robot_get_ui_sounds_muted(void) {
    return robot_cfg.ui_sounds_muted;
}

int robot_save_config(void) {
    /* Try to write robots.json to ux0: (most common writable location) */
    FILE* f = fopen("ux0:/data/RobotConsole/robots.json", "w");
    if (!f) {
        /* Fallback to root directory */
        f = fopen("robots.json", "w");
        if (!f) return -1;
    }

    fprintf(f, "{\n  \"robots\": [\n");
    for (int i = 0; i < robot_cfg.count; i++) {
        fprintf(f, "    {\n");
        fprintf(f, "      \"ip\": \"%s\",\n", robot_cfg.ips[i]);
        if (robot_cfg.camera_hosts[i][0])
            fprintf(f, "      \"camera_host\": \"%s\",\n", robot_cfg.camera_hosts[i]);
        fprintf(f, "      \"camera_port\": %d,\n", robot_cfg.camera_ports[i]);
        fprintf(f, "      \"stream_endpoint\": \"%s\",\n", robot_cfg.stream_endpoints[i]);
        fprintf(f, "      \"speed_preset\": %d\n", robot_cfg.speed_presets[i]);
        fprintf(f, "    }");
        if (i < robot_cfg.count - 1)
            fprintf(f, ",");
        fprintf(f, "\n");
    }
    fprintf(f, "  ],\n");
    fprintf(f, "  \"ui_sounds_muted\": %d\n", robot_cfg.ui_sounds_muted);
    fprintf(f, "}\n");
    fclose(f);
    printf("[ROBOT] Config saved to disk\n");
    return 0;
}