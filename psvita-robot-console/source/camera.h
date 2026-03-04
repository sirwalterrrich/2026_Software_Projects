#ifndef CAMERA_H
#define CAMERA_H

typedef struct {
    char stream_endpoint[64];
    char stream_host[64];     /* e.g., "artillery:5001" or "192.168.1.100:5000" */
    int quality;              /* 1-100 */
    int fps;                  /* target frames per second */
    int timeout_ms;           /* connection timeout */
    char resolution[32];      /* e.g., "640x480" */
    int buffer_size;          /* frame buffer size in bytes */
    char boundary[64];        /* MJPEG boundary marker */
} CameraConfig;

int camera_config_load(void);
const CameraConfig* camera_get_config(void);
int camera_get_stream_width(void);
int camera_get_stream_height(void);

#endif
