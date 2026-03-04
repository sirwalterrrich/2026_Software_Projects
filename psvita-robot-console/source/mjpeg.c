#include "mjpeg.h"
#include "log.h"
#include <curl/curl.h>
#include <jpeglib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

#define MJPEG_BUFFER_SIZE (768 * 1024)  /* 768KB per frame (640x360 RGB = 691KB) */
#define MAX_FRAMES 2
#define MAX_JPEG_SIZE (256 * 1024)  /* Max single JPEG download */

static int mjpeg_frame_count = 0;

typedef struct {
    uint8_t* data[MAX_FRAMES];
    int size[MAX_FRAMES];
    int width;
    int height;
    volatile int write_idx;
    volatile int latest_ready_idx;
    volatile int status;  /* 0=OK, 1=SLOW, 2=FAIL */
    int initialized;
} MJPEGBuffer;

typedef struct {
    char url[256];        /* Stream URL (for config display) */
    char snap_url[256];   /* Snapshot URL (for actual fetching) */
    char boundary[64];
    MJPEGBuffer buf;
    SceUID fetch_thread;
    volatile int running;
    volatile int should_stop;
    volatile int initialized;
} MJPEGContext;

static MJPEGContext mjpeg_ctx = {0};

/* Decode JPEG data to RGB raw pixels using libjpeg. */
static uint8_t* decode_jpeg(const uint8_t* jpeg_data, int jpeg_size,
                             int* out_width, int* out_height) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    uint8_t* rgb_data = NULL;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, (uint8_t*)jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);

    if (cinfo.image_width > 960 || cinfo.image_height > 544) {
        cinfo.scale_num = 1;
        cinfo.scale_denom = 2;
    }

    jpeg_start_decompress(&cinfo);

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int components = cinfo.output_components;

    int row_stride = width * 3;
    rgb_data = malloc(height * row_stride);
    if (!rgb_data) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    uint8_t* buffer = malloc(width * components + 256);
    if (!buffer) {
        free(rgb_data);
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    int row = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, (JSAMPARRAY)&buffer, 1);

        uint8_t* out = rgb_data + (row * row_stride);
        if (components == 3) {
            memcpy(out, buffer, row_stride);
        } else if (components == 1) {
            for (int i = 0; i < width; i++) {
                out[i*3+0] = buffer[i];
                out[i*3+1] = buffer[i];
                out[i*3+2] = buffer[i];
            }
        }
        row++;
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    free(buffer);

    *out_width = width;
    *out_height = height;
    return rgb_data;
}

/* Simple write callback for snapshot: just accumulate JPEG bytes */
typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} SnapBuffer;

static size_t snap_write_cb(const uint8_t* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    SnapBuffer* buf = (SnapBuffer*)userdata;

    if (buf->size + total > buf->capacity) return 0;  /* Abort if too large */
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    return total;
}

/* Build snapshot URL from stream URL.
 * Replaces "action=stream" with "action=snapshot" for mjpg-streamer.
 * If no such pattern, appends "/snapshot" or returns URL as-is. */
static void build_snap_url(const char* stream_url, char* snap_url, size_t snap_url_size) {
    /* Try to replace action=stream with action=snapshot */
    const char* action = strstr(stream_url, "action=stream");
    if (action) {
        size_t prefix_len = action - stream_url;
        snprintf(snap_url, snap_url_size, "%.*saction=snapshot%s",
                 (int)prefix_len, stream_url, action + strlen("action=stream"));
        return;
    }
    /* Fallback: use stream URL as-is (works for servers that return single JPEG) */
    strncpy(snap_url, stream_url, snap_url_size - 1);
    snap_url[snap_url_size - 1] = '\0';
}

/* Snapshot polling thread: fetch one JPEG at a time, decode, repeat.
 * No buffering, no backlog — latency = 1 round trip + decode time. */
static int mjpeg_fetch_thread_func(SceSize args, void* argp) {
    (void)args; (void)argp;
    MJPEGContext* ctx = &mjpeg_ctx;

    log_write("[MJPEG-thread] Thread started, snap URL: %s", ctx->snap_url);

    /* Reusable JPEG download buffer */
    uint8_t* jpeg_buf = malloc(MAX_JPEG_SIZE);
    if (!jpeg_buf) {
        log_write("[MJPEG-thread] JPEG buffer alloc FAILED");
        ctx->buf.status = 2;
        return -1;
    }

    /* Reuse a single CURL handle for all requests (connection keep-alive) */
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_write("[MJPEG-thread] CURL init failed!");
        free(jpeg_buf);
        ctx->buf.status = 2;
        return -1;
    }

    SnapBuffer snap_buf = { .data = jpeg_buf, .size = 0, .capacity = MAX_JPEG_SIZE };

    curl_easy_setopt(curl, CURLOPT_URL, ctx->snap_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, snap_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &snap_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    log_write("[MJPEG-thread] Starting snapshot polling loop");

    while (!ctx->should_stop) {
        snap_buf.size = 0;  /* Reset for new fetch */

        CURLcode res = curl_easy_perform(curl);

        if (ctx->should_stop) break;

        if (res != CURLE_OK) {
            if (mjpeg_frame_count == 0) {
                log_write("[MJPEG-thread] Fetch error: %d (%s)", res, curl_easy_strerror(res));
            }
            ctx->buf.status = 2;
            sceKernelDelayThread(500000);  /* 500ms before retry on error */
            continue;
        }

        if (snap_buf.size < 100) {
            sceKernelDelayThread(100000);  /* 100ms — bad response */
            continue;
        }

        /* Decode the JPEG */
        int width = 0, height = 0;
        uint8_t* rgb_data = decode_jpeg(snap_buf.data, snap_buf.size, &width, &height);

        if (rgb_data && width > 0 && height > 0) {
            int rgb_size = width * height * 3;

            if (rgb_size <= MJPEG_BUFFER_SIZE) {
                int frame_idx = ctx->buf.write_idx;
                memcpy(ctx->buf.data[frame_idx], rgb_data, rgb_size);
                ctx->buf.size[frame_idx] = rgb_size;
                ctx->buf.width = width;
                ctx->buf.height = height;

                ctx->buf.latest_ready_idx = frame_idx;
                ctx->buf.status = 0;
                ctx->buf.write_idx = (frame_idx + 1) % MAX_FRAMES;

                mjpeg_frame_count++;
                if (mjpeg_frame_count == 1 || mjpeg_frame_count % 100 == 0) {
                    log_write("[MJPEG] Frame %d: %dx%d (%d bytes JPEG, %d bytes RGB)",
                              mjpeg_frame_count, width, height, (int)snap_buf.size, rgb_size);
                }
            }
            free(rgb_data);
        }

        /* No delay — immediately fetch next frame for lowest latency.
         * The fetch + decode time naturally throttles to ~5-10fps. */
    }

    curl_easy_cleanup(curl);
    free(jpeg_buf);
    log_write("[MJPEG-thread] Thread exiting, decoded %d frames total", mjpeg_frame_count);

    return 0;
}

int mjpeg_init(const char* stream_url, const char* boundary) {
    log_write("[MJPEG-init] called, initialized=%d", mjpeg_ctx.initialized);

    if (mjpeg_ctx.initialized) {
        log_write("[MJPEG-init] Already initialized, returning");
        return 0;
    }

    if (!stream_url || !stream_url[0]) {
        log_write("[MJPEG-init] ERROR: no stream URL provided");
        return -1;
    }

    strncpy(mjpeg_ctx.url, stream_url, sizeof(mjpeg_ctx.url) - 1);
    mjpeg_ctx.url[sizeof(mjpeg_ctx.url) - 1] = '\0';

    /* Build snapshot URL from stream URL */
    build_snap_url(stream_url, mjpeg_ctx.snap_url, sizeof(mjpeg_ctx.snap_url));

    if (boundary && boundary[0]) {
        strncpy(mjpeg_ctx.boundary, boundary, sizeof(mjpeg_ctx.boundary) - 1);
        mjpeg_ctx.boundary[sizeof(mjpeg_ctx.boundary) - 1] = '\0';
    } else {
        strcpy(mjpeg_ctx.boundary, "frame");
    }

    log_write("[MJPEG-init] Stream URL: %s", mjpeg_ctx.url);
    log_write("[MJPEG-init] Snap URL:   %s", mjpeg_ctx.snap_url);

    for (int i = 0; i < MAX_FRAMES; i++) {
        mjpeg_ctx.buf.data[i] = malloc(MJPEG_BUFFER_SIZE);
        if (!mjpeg_ctx.buf.data[i]) {
            log_write("[MJPEG-init] Buffer alloc FAILED for frame %d", i);
            return -1;
        }
        mjpeg_ctx.buf.size[i] = 0;
    }
    log_write("[MJPEG-init] Allocated %d frame buffers (%d bytes each)", MAX_FRAMES, MJPEG_BUFFER_SIZE);

    mjpeg_ctx.buf.write_idx = 0;
    mjpeg_ctx.buf.latest_ready_idx = -1;
    mjpeg_ctx.buf.status = 2;
    mjpeg_ctx.running = 0;
    mjpeg_ctx.should_stop = 0;
    mjpeg_ctx.initialized = 1;

    log_write("[MJPEG-init] Initialized OK");
    return 0;
}

int mjpeg_start(void) {
    if (!mjpeg_ctx.initialized) {
        log_write("[MJPEG-start] Not initialized, returning -1");
        return -1;
    }
    if (mjpeg_ctx.running) {
        log_write("[MJPEG-start] Already running");
        return 0;
    }

    mjpeg_ctx.should_stop = 0;
    mjpeg_ctx.running = 1;
    mjpeg_frame_count = 0;

    mjpeg_ctx.fetch_thread = sceKernelCreateThread(
        "mjpeg_fetch",
        mjpeg_fetch_thread_func,
        0x10000100,
        0x10000,  /* 64KB stack */
        0, 0, NULL
    );

    if (mjpeg_ctx.fetch_thread < 0) {
        log_write("[MJPEG-start] Thread creation FAILED: 0x%08X", mjpeg_ctx.fetch_thread);
        mjpeg_ctx.running = 0;
        return -1;
    }

    log_write("[MJPEG-start] Thread created (ID: 0x%08X), starting...", mjpeg_ctx.fetch_thread);
    int ret = sceKernelStartThread(mjpeg_ctx.fetch_thread, 0, NULL);
    log_write("[MJPEG-start] sceKernelStartThread returned: 0x%08X", ret);
    return 0;
}

void mjpeg_stop(void) {
    if (!mjpeg_ctx.running) return;

    log_write("[MJPEG-stop] Requesting stop...");
    mjpeg_ctx.should_stop = 1;

    SceUInt timeout = 5000000;  /* 5s timeout — snapshot may be mid-request */
    sceKernelWaitThreadEnd(mjpeg_ctx.fetch_thread, NULL, &timeout);
    sceKernelDeleteThread(mjpeg_ctx.fetch_thread);
    mjpeg_ctx.running = 0;
    log_write("[MJPEG-stop] Stopped");
}

MJPEGFrame* mjpeg_get_frame(void) {
    if (!mjpeg_ctx.initialized || mjpeg_ctx.buf.latest_ready_idx < 0)
        return NULL;

    MJPEGFrame* frame = malloc(sizeof(MJPEGFrame));
    if (!frame) return NULL;

    frame->timestamp = sceKernelGetSystemTimeWide();
    frame->width = mjpeg_ctx.buf.width;
    frame->height = mjpeg_ctx.buf.height;

    return frame;
}

const uint8_t* mjpeg_get_frame_data(int* out_width, int* out_height) {
    if (!mjpeg_ctx.initialized) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
        return NULL;
    }

    int idx = mjpeg_ctx.buf.latest_ready_idx;
    if (idx >= 0 && idx < MAX_FRAMES && mjpeg_ctx.buf.size[idx] > 0) {
        if (out_width) *out_width = mjpeg_ctx.buf.width;
        if (out_height) *out_height = mjpeg_ctx.buf.height;
        return mjpeg_ctx.buf.data[idx];
    }

    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
    return NULL;
}

void mjpeg_release_frame(MJPEGFrame* frame) {
    if (frame) free(frame);
}

int mjpeg_get_status(void) {
    if (!mjpeg_ctx.initialized) return 2;
    return mjpeg_ctx.buf.status;
}

int mjpeg_get_frame_counter(void) {
    return mjpeg_frame_count;
}

void mjpeg_cleanup(void) {
    if (!mjpeg_ctx.initialized) return;

    mjpeg_stop();

    for (int i = 0; i < MAX_FRAMES; i++) {
        if (mjpeg_ctx.buf.data[i]) {
            free(mjpeg_ctx.buf.data[i]);
            mjpeg_ctx.buf.data[i] = NULL;
        }
    }

    mjpeg_ctx.initialized = 0;
}
