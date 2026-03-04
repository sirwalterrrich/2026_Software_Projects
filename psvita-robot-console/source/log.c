#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static FILE* _log_fp = NULL;

/* Ring buffer for recent log lines (on-screen viewer) */
#define LOG_RING_SIZE 20
static char log_ring[LOG_RING_SIZE][128];
static int log_ring_head = 0;
static int log_ring_count = 0;

void log_init(void) {
    _log_fp = fopen(LOG_PATH, "w");
    if (_log_fp) {
        fprintf(_log_fp, "=== Robot Console Log ===\n");
        fflush(_log_fp);
    }
}

void log_write(const char* fmt, ...) {
    va_list ap;

    /* Store in ring buffer */
    va_start(ap, fmt);
    vsnprintf(log_ring[log_ring_head], 128, fmt, ap);
    va_end(ap);
    log_ring_head = (log_ring_head + 1) % LOG_RING_SIZE;
    if (log_ring_count < LOG_RING_SIZE) log_ring_count++;

    /* Write to file */
    if (!_log_fp) return;
    va_start(ap, fmt);
    vfprintf(_log_fp, fmt, ap);
    va_end(ap);
    fprintf(_log_fp, "\n");
    fflush(_log_fp);
}

void log_close(void) {
    if (_log_fp) {
        fclose(_log_fp);
        _log_fp = NULL;
    }
}

int log_get_recent(char lines[][128], int max_lines) {
    int count = log_ring_count < max_lines ? log_ring_count : max_lines;
    /* Copy oldest-first */
    int start = (log_ring_head - log_ring_count + LOG_RING_SIZE) % LOG_RING_SIZE;
    int skip = log_ring_count - count;
    start = (start + skip) % LOG_RING_SIZE;
    for (int i = 0; i < count; i++) {
        memcpy(lines[i], log_ring[(start + i) % LOG_RING_SIZE], 128);
    }
    return count;
}
