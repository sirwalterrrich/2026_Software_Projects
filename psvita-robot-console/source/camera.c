#include "camera.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CONFIG_PATHS_COUNT 4

static CameraConfig camera_cfg = {0};
static int camera_initialized = 0;

/* Try multiple paths for config file */
static const char* const CONFIG_PATHS[] = {
    "app0:/camera.json",
    "ux0:/data/RobotConsole/camera.json",
    "camera.json",
    "assets/camera.json"
};

static void set_defaults(void) {
    strcpy(camera_cfg.stream_endpoint, "/stream");
    strcpy(camera_cfg.stream_host, "localhost:5000");
    camera_cfg.quality = 80;
    camera_cfg.fps = 30;
    camera_cfg.timeout_ms = 5000;
    strcpy(camera_cfg.resolution, "640x480");
    camera_cfg.buffer_size = 65536;
    strcpy(camera_cfg.boundary, "frame");
}

static int parse_int_from_json(const char* json_str, const char* key, int default_val) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":%d", key, 0);  /* template */
    
    const char* ptr = strstr(json_str, key);
    if (!ptr) return default_val;
    
    ptr = strchr(ptr, ':');
    if (!ptr) return default_val;
    ptr++;
    
    /* Skip whitespace */
    while (*ptr == ' ' || *ptr == '\t') ptr++;
    
    int val = 0;
    if (sscanf(ptr, "%d", &val) == 1) {
        return val;
    }
    return default_val;
}

static int parse_string_from_json(const char* json_str, const char* key, char* out, int out_size) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    printf("[JSON-parse] Looking for key: %s\n", search_key);
    
    const char* ptr = strstr(json_str, search_key);
    if (!ptr) {
        printf("[JSON-parse] Key not found: %s\n", search_key);
        return -1;
    }
    
    printf("[JSON-parse] Found key at %p\n", (void*)ptr);
    
    /* Find the colon and opening quote */
    ptr = strchr(ptr, ':');
    if (!ptr) {
        printf("[JSON-parse] No colon found after key\n");
        return -1;
    }
    
    ptr = strchr(ptr, '"');
    if (!ptr) {
        printf("[JSON-parse] No opening quote found\n");
        return -1;
    }
    ptr++;  /* skip opening quote */
    
    /* Extract string up to closing quote */
    const char* end = strchr(ptr, '"');
    if (!end) {
        printf("[JSON-parse] No closing quote found\n");
        return -1;
    }
    
    int len = end - ptr;
    if (len >= out_size) len = out_size - 1;
    
    strncpy(out, ptr, len);
    out[len] = '\0';
    
    printf("[JSON-parse] Parsed %s = '%s' (len: %d)\n", key, out, len);
    return 0;
}

static int parse_json_config(const char* json_str) {
    if (!json_str || !json_str[0]) return -1;
    
    /* Parse string values */
    if (parse_string_from_json(json_str, "stream_endpoint", 
                               camera_cfg.stream_endpoint, 
                               sizeof(camera_cfg.stream_endpoint)) == 0) {
        /* Successfully parsed endpoint */
    }
    
    if (parse_string_from_json(json_str, "stream_host", 
                               camera_cfg.stream_host, 
                               sizeof(camera_cfg.stream_host)) == 0) {
        /* Successfully parsed stream host */
    }
    
    if (parse_string_from_json(json_str, "boundary", 
                               camera_cfg.boundary, 
                               sizeof(camera_cfg.boundary)) == 0) {
        /* Successfully parsed boundary marker */
    }
    
    if (parse_string_from_json(json_str, "resolution", 
                               camera_cfg.resolution, 
                               sizeof(camera_cfg.resolution)) == 0) {
        /* Successfully parsed resolution */
    }
    
    /* Parse integer values */
    int quality = parse_int_from_json(json_str, "quality", 80);
    if (quality >= 1 && quality <= 100)
        camera_cfg.quality = quality;
    
    int fps = parse_int_from_json(json_str, "fps", 30);
    if (fps >= 1 && fps <= 60)
        camera_cfg.fps = fps;
    
    int timeout = parse_int_from_json(json_str, "timeout_ms", 5000);
    if (timeout >= 100 && timeout <= 30000)
        camera_cfg.timeout_ms = timeout;
    
    int buffer = parse_int_from_json(json_str, "buffer_size", 65536);
    if (buffer >= 8192 && buffer <= 1048576)
        camera_cfg.buffer_size = buffer;
    
    return 0;
}

int camera_config_load(void) {
    if (camera_initialized) return 0;
    
    printf("[CAMERA] Loading config...\n");
    
    /* Set defaults first */
    set_defaults();
    printf("[CAMERA] Defaults: endpoint=%s, host=%s, boundary=%s\n", 
           camera_cfg.stream_endpoint, camera_cfg.stream_host, camera_cfg.boundary);
    
    /* Try each config path */
    for (int i = 0; i < CONFIG_PATHS_COUNT; i++) {
        printf("[CAMERA] Trying path: %s\n", CONFIG_PATHS[i]);
        FILE* f = fopen(CONFIG_PATHS[i], "r");
        if (!f) {
            printf("[CAMERA] File not found: %s\n", CONFIG_PATHS[i]);
            continue;
        }
        
        /* Read file into buffer */
        char buf[2048] = {0};
        size_t bytes_read = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        
        if (bytes_read > 0) {
            buf[bytes_read] = '\0';
            printf("[CAMERA] Read %d bytes from %s\n", (int)bytes_read, CONFIG_PATHS[i]);
            if (parse_json_config(buf) == 0) {
                printf("[CAMERA] Config loaded successfully\n");
                printf("[CAMERA] Final values - Host: %s, Endpoint: %s, Boundary: %s\n",
                       camera_cfg.stream_host, camera_cfg.stream_endpoint, camera_cfg.boundary);
                camera_initialized = 1;
                return 0;  /* Success */
            }
        }
    }
    
    /* Loaded with defaults (fallback) */
    camera_initialized = 1;
    return -1;
}

const CameraConfig* camera_get_config(void) {
    if (!camera_initialized)
        camera_config_load();
    return &camera_cfg;
}

int camera_get_stream_width(void) {
    if (!camera_initialized)
        camera_config_load();
    
    int w = 0;
    sscanf(camera_cfg.resolution, "%dx", &w);
    return w > 0 ? w : 640;
}

int camera_get_stream_height(void) {
    if (!camera_initialized)
        camera_config_load();
    
    int w, h;
    if (sscanf(camera_cfg.resolution, "%dx%d", &w, &h) == 2) {
        return h > 0 ? h : 480;
    }
    return 480;
}
