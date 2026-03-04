#ifndef MJPEG_H
#define MJPEG_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    uint32_t timestamp;
} MJPEGFrame;

/* Initialize MJPEG streaming with a full stream URL and boundary marker */
int mjpeg_init(const char* stream_url, const char* boundary);

/* Start background fetch thread (non-blocking) */
int mjpeg_start(void);

/* Stop the fetch thread cleanly */
void mjpeg_stop(void);

/* Get the latest frame RGB data (returns pointer to buffer, NULL if not ready) */
const uint8_t* mjpeg_get_frame_data(int* out_width, int* out_height);

/* Get the latest frame (may return NULL if no frame ready) */
MJPEGFrame* mjpeg_get_frame(void);

/* Release frame when done with it */
void mjpeg_release_frame(MJPEGFrame* frame);

/* Get connection status (0=OK, 1=SLOW, 2=FAIL) */
int mjpeg_get_status(void);

/* Get monotonic frame counter (increments each decoded frame) */
int mjpeg_get_frame_counter(void);

/* Cleanup MJPEG resources */
void mjpeg_cleanup(void);

#endif
