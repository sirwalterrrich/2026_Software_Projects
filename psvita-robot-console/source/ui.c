#include "ui.h"
#include "input.h"
#include "network.h"
#include "robot.h"
#include "mjpeg.h"
#include "log.h"
#include "feedback.h"
#include "version.h"
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/gxm.h>
#include <psp2/display.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <png.h>

static vita2d_pgf* pgf_font = NULL;
static uint32_t estop_flash_start;
static vita2d_texture* camera_texture = NULL;
static int camera_tex_width = 0;
static int camera_tex_height = 0;

/* Configuration editing state */
static int config_selected_robot = 0;
static int config_selected_field = 0;  /* 0=IP, 1=CAMERA_HOST, 2=PORT, 3=ENDPOINT */
static int config_editing = 0;

/* IME dialog state */
static SceWChar16 ime_input_buf[128];
static SceWChar16 ime_initial_buf[128];
static char ime_title_buf[64];
static int ime_active = 0;

/* Touch tap detection for robot name */
static int touch_was_active = 0;
static int touch_tap_x = 0;
static int touch_tap_y = 0;
static int robot_name_tap = 0;

/* Speed preset */
static int speed_preset = SPEED_FAST;  /* default: full speed */
static const float speed_scales[] = { 0.3f, 0.6f, 1.0f };
static const char* speed_labels[] = { "SLOW", "MED", "FAST" };

/* Screenshot counter — initialized on first use by scanning existing files */
static int screenshot_count = -1;

/* Pending screenshot flag (taken between end_drawing and swap) */
static int screenshot_pending = 0;

/* Screenshot flash feedback */
static uint32_t screenshot_flash_start = 0;

/* FPS counter */
static uint32_t fps_last_time = 0;
static int fps_frame_count = 0;
static int fps_display = 0;

/* Low battery warning */
static uint32_t batt_warn_start = 0;

/* Auto-dim HUD: last input activity time */
static uint32_t last_input_time = 0;
#define HUD_DIM_DELAY_MS 5000
#define HUD_DIM_ALPHA    80   /* alpha for dimmed HUD elements (0-255) */

/* Log viewer */
static int log_viewer_active = 0;

/* Config test connection result */
static int config_test_result = -1;  /* -1=not tested, 0=fail, 1=ok */
static uint32_t config_test_time = 0;

/* Config save confirmation flash (shown on control screen) */
static uint32_t config_save_flash_time = 0;

/* Config field validation feedback */
static char config_validation_msg[64] = {0};
static unsigned int config_validation_col = 0;
static uint32_t config_validation_time = 0;

/* Validate IPv4 address format (x.x.x.x, each octet 0-255) */
static int validate_ip(const char* ip) {
    if (!ip || !ip[0]) return 0;
    int octets[4];
    int n = sscanf(ip, "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]);
    if (n != 4) return 0;
    for (int i = 0; i < 4; i++) {
        if (octets[i] < 0 || octets[i] > 255) return 0;
    }
    /* Check no trailing junk */
    char check[32];
    snprintf(check, sizeof(check), "%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]);
    return (strcmp(check, ip) == 0) ? 1 : 0;
}

static void config_set_validation(const char* msg, unsigned int col) {
    strncpy(config_validation_msg, msg, sizeof(config_validation_msg) - 1);
    config_validation_msg[sizeof(config_validation_msg) - 1] = '\0';
    config_validation_col = col;
    config_validation_time = (uint32_t)(sceKernelGetProcessTimeWide() / 1000);
}

/* === Screen and layout === */
#define SCREEN_W      960
#define SCREEN_H      544
#define TOP_BAR_H     60
#define BOTTOM_BAR_H  60
#define CENTER_H      (SCREEN_H - TOP_BAR_H - BOTTOM_BAR_H)
#define CENTER_Y_MID  (TOP_BAR_H + CENTER_H / 2)
#define CENTER_X_MID  (SCREEN_W / 2)
#define FONT_SIZE     18
#define PGF_BASELINE_OFFSET 17

/* Reticle */
#define RETICLE_R_OUTER  100
#define RETICLE_R_INNER  50
#define RETICLE_DOT_R    4
#define RETICLE_LINE_LEN 115
#define RETICLE_GAP      8
#define TICK_LEN         12

/* LIN slider (left side) */
#define LIN_X            60
#define LIN_Y_TOP        (CENTER_Y_MID - 120)
#define LIN_HEIGHT       240
#define LIN_WIDTH        20

/* ANG slider (bottom center) */
#define ANG_Y            (CENTER_Y_MID + 140)
#define ANG_X_LEFT       (CENTER_X_MID - 140)
#define ANG_WIDTH        280
#define ANG_HEIGHT       20

/* LIFT buttons (right side) */
#define LIFT_X           (SCREEN_W - 80)
#define LIFT_BTN_W       50
#define LIFT_BTN_H       40
#define LIFT_UP_Y        (CENTER_Y_MID - 70)
#define LIFT_DOWN_Y      (CENTER_Y_MID + 30)

/* === Timing helper === */
static inline uint32_t get_ticks_ms(void) {
    return (uint32_t)(sceKernelGetProcessTimeWide() / 1000);
}

/* === HUD Color Palette (vita2d RGBA8 format) === */
#define COL_GREEN        RGBA8(  0, 200,  80, 255)
#define COL_GREEN_BRIGHT RGBA8(  0, 230, 100, 255)
#define COL_GREEN_DIM    RGBA8(  0, 120,  50, 255)
#define COL_AMBER        RGBA8(220, 180,   0, 255)
#define COL_RED          RGBA8(220,  50,  50, 255)
#define COL_WHITE        RGBA8(220, 220, 230, 255)
#define COL_DIM          RGBA8(120, 120, 140, 255)
#define COL_TRACK        RGBA8( 35,  35,  45, 255)
#define COL_TRACK_BORDER RGBA8( 50,  60,  55, 255)
#define COL_BAR_BG       RGBA8( 20,  20,  28, 255)
#define COL_DARK_TEXT    RGBA8( 10,  10,  15, 255)

/* PlayStation canonical button colors */
#define COL_PS_CROSS     RGBA8( 80, 130, 220, 255)   /* Blue */
#define COL_PS_CIRCLE    RGBA8(220,  50,  50, 255)    /* Red */
#define COL_PS_TRIANGLE  RGBA8(  0, 200,  80, 255)    /* Green */
#define COL_PS_SQUARE    RGBA8(200, 100, 180, 255)    /* Pink */

/* === Helper: outline rectangle (vita2d has no built-in) === */
static void draw_outline_rect(int x, int y, int w, int h, unsigned int color) {
    vita2d_draw_line(x, y, x + w - 1, y, color);
    vita2d_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    vita2d_draw_line(x, y, x, y + h - 1, color);
    vita2d_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

/* 5x7 block font for fallback when PGF fails (rows, 5 LSBs per row) */
static const unsigned char block_font[][7] = {
    /* 0-9: digits */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /*  0: 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, /*  1: 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, /*  2: 2 */
    { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E }, /*  3: 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /*  4: 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, /*  5: 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /*  6: 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04 }, /*  7: 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, /*  8: 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, /*  9: 9 */
    /* 10-15: original special chars */
    { 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00 }, /* 10: . */
    { 0x11, 0x02, 0x04, 0x08, 0x10, 0x11, 0x00 }, /* 11: % */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 12: space */
    { 0x00, 0x10, 0x1A, 0x15, 0x11, 0x11, 0x11 }, /* 13: m (lowercase) */
    { 0x00, 0x00, 0x0E, 0x11, 0x10, 0x0E, 0x00 }, /* 14: s (lowercase) */
    { 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00 }, /* 15: - */
    /* 16-41: A-Z uppercase */
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* 16: A */
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* 17: B */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* 18: C */
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, /* 19: D */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* 20: E */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, /* 21: F */
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, /* 22: G */
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* 23: H */
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* 24: I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* 25: J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* 26: K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* 27: L */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* 28: M */
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, /* 29: N */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* 30: O */
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, /* 31: P */
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, /* 32: Q */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* 33: R */
    { 0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E }, /* 34: S */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* 35: T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* 36: U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, /* 37: V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, /* 38: W */
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* 39: X */
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, /* 40: Y */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, /* 41: Z */
    /* 42+: extra symbols */
    { 0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x00 }, /* 42: ^ (up arrow) */
    { 0x00, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04 }, /* 43: v (down arrow) */
    { 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00 }, /* 44: : */
    { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }, /* 45: + */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F }, /* 46: _ */
    { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, /* 47: > */
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, /* 48: < */
    { 0x06, 0x08, 0x08, 0x08, 0x08, 0x08, 0x06 }, /* 49: ( */
    { 0x0C, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0C }, /* 50: ) */
    { 0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 }, /* 51: / */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, /* 52: ? */
    { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 }, /* 53: = */
};

#define BLOCK_W 5
#define BLOCK_H 7
#define BLOCK_SCALE 2
#define BLOCK_CHAR_W ((BLOCK_W + 1) * BLOCK_SCALE)

static void draw_block_char(int x, int y, int ch, unsigned int color) {
    int idx = -1;
    if (ch >= '0' && ch <= '9') idx = ch - '0';
    else if (ch == '.') idx = 10;
    else if (ch == '%') idx = 11;
    else if (ch == ' ' || ch == '\0') idx = 12;
    else if (ch == 'm') idx = 13;
    else if (ch == 's') idx = 14;
    else if (ch == '-') idx = 15;
    else if (ch >= 'A' && ch <= 'Z') idx = 16 + (ch - 'A');
    else if (ch == '^') idx = 42;
    else if (ch == 'v') idx = 43;
    else if (ch >= 'a' && ch <= 'z') idx = 16 + (ch - 'a');
    else if (ch == ':') idx = 44;
    else if (ch == '+') idx = 45;
    else if (ch == '_') idx = 46;
    else if (ch == '>') idx = 47;
    else if (ch == '<') idx = 48;
    else if (ch == '(') idx = 49;
    else if (ch == ')') idx = 50;
    else if (ch == '/') idx = 51;
    else if (ch == '?') idx = 52;
    else if (ch == '=') idx = 53;
    if (idx < 0) return;
    for (int row = 0; row < BLOCK_H; row++) {
        unsigned char r = block_font[idx][row];
        for (int col = 0; col < BLOCK_W; col++) {
            if (r & (1 << (BLOCK_W - 1 - col))) {
                vita2d_draw_rectangle(x + col * BLOCK_SCALE, y + row * BLOCK_SCALE,
                                     BLOCK_SCALE, BLOCK_SCALE, color);
            }
        }
    }
}

static void draw_text_fallback(int x, int y, const char* text, unsigned int color) {
    if (!text) return;
    for (int i = 0; text[i]; i++)
        draw_block_char(x + i * BLOCK_CHAR_W, y, (unsigned char)text[i], color);
}

static void draw_text(int x, int y, const char* text, unsigned int color) {
    if (!text || !text[0]) return;
    if (pgf_font) {
        vita2d_pgf_draw_text(pgf_font, x, y + PGF_BASELINE_OFFSET, color, 1.0f, text);
        return;
    }
    draw_text_fallback(x, y, text, color);
}

/* === HUD Drawing Utilities === */

static int measure_text(const char* text) {
    if (!text || !text[0]) return 0;
    if (pgf_font) {
        return vita2d_pgf_text_width(pgf_font, 1.0f, text);
    }
    return (int)strlen(text) * BLOCK_CHAR_W;
}

/* Line-segment circle — avoids vita2d_draw_pixel artifacts */
#define CIRCLE_SEGMENTS 64
static void draw_circle(int cx, int cy, int r, unsigned int color) {
    float step = 2.0f * 3.14159265f / CIRCLE_SEGMENTS;
    float prev_x = cx + r;
    float prev_y = cy;
    for (int i = 1; i <= CIRCLE_SEGMENTS; i++) {
        float a = i * step;
        float nx = cx + r * cosf(a);
        float ny = cy + r * sinf(a);
        vita2d_draw_line(prev_x, prev_y, nx, ny, color);
        prev_x = nx;
        prev_y = ny;
    }
}

static void draw_circle_thick(int cx, int cy, int r, int thickness, unsigned int color) {
    int half = thickness / 2;
    for (int i = -half; i <= half; i++) {
        if (r + i > 0)
            draw_circle(cx, cy, r + i, color);
    }
}

static void draw_filled_circle(int cx, int cy, int r, unsigned int color) {
    /* Horizontal line fill — avoids vita2d_draw_fill_circle artifacts */
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        vita2d_draw_line(cx - dx, cy + dy, cx + dx, cy + dy, color);
    }
}

static void draw_rounded_rect_filled(int x, int y, int w, int h, int r, unsigned int color) {
    /* Center body */
    vita2d_draw_rectangle(x + r, y, w - 2 * r, h, color);
    /* Left strip */
    vita2d_draw_rectangle(x, y + r, r, h - 2 * r, color);
    /* Right strip */
    vita2d_draw_rectangle(x + w - r, y + r, r, h - 2 * r, color);
    /* Corner circles */
    draw_filled_circle(x + r,     y + r,     r, color);
    draw_filled_circle(x + w - r, y + r,     r, color);
    draw_filled_circle(x + r,     y + h - r, r, color);
    draw_filled_circle(x + w - r, y + h - r, r, color);
}

void ui_init(void) {
    estop_flash_start = 0;
    pgf_font = NULL;
    camera_texture = NULL;
    pgf_font = vita2d_load_default_pgf();
    if (!pgf_font)
        printf("ui: PGF font load failed, using block font fallback\n");
    else
        printf("ui: loaded system PGF font\n");
}

void ui_cleanup(void) {
    if (camera_texture) {
        vita2d_free_texture(camera_texture);
        camera_texture = NULL;
    }
    if (pgf_font) {
        vita2d_free_pgf(pgf_font);
        pgf_font = NULL;
    }
}

void ui_set_speed_preset(int preset) {
    if (preset >= 0 && preset < SPEED_COUNT)
        speed_preset = preset;
}

int ui_get_speed_preset(void) {
    return speed_preset;
}

float ui_get_speed_scale(void) {
    return speed_scales[speed_preset];
}

void ui_notify_config_saved(void) {
    config_save_flash_time = (uint32_t)(sceKernelGetProcessTimeWide() / 1000);
}

void ui_toggle_log_viewer(void) {
    log_viewer_active = !log_viewer_active;
}

void ui_draw_splash(const char* status_text) {
    vita2d_start_drawing();
    vita2d_set_clear_color(RGBA8(8, 10, 22, 255));
    vita2d_clear_screen();

    /* Color palette */
    unsigned int col_grid    = RGBA8(20, 25, 50, 255);
    unsigned int col_body    = RGBA8(30, 80, 160, 255);
    unsigned int col_bright  = RGBA8(60, 150, 220, 255);
    unsigned int col_dark    = RGBA8(18, 22, 45, 255);
    unsigned int col_track   = RGBA8(22, 28, 55, 255);
    unsigned int col_tread   = RGBA8(35, 45, 75, 255);
    unsigned int col_cyan    = RGBA8(0, 200, 255, 255);
    unsigned int col_title   = RGBA8(60, 150, 220, 255);
    unsigned int col_sub     = RGBA8(80, 100, 140, 255);

    /* Background grid */
    for (int gy = 0; gy < SCREEN_H; gy += 24)
        vita2d_draw_line(0, gy, SCREEN_W, gy, col_grid);
    for (int gx = 0; gx < SCREEN_W; gx += 24)
        vita2d_draw_line(gx, 0, gx, SCREEN_H, col_grid);

    /* === Robot centered at (480, 185) === */
    int rx = 480, ry = 185;

    /* --- Tracks (left and right) --- */
    int tw = 120, th = 40;
    int track_gap = 90; /* distance between track centers */

    /* Left track */
    int lt_x = rx - track_gap / 2 - tw / 2;
    int lt_y = ry + 10;
    vita2d_draw_rectangle(lt_x, lt_y, tw, th, col_track);
    draw_outline_rect(lt_x, lt_y, tw, th, col_body);
    /* Tread segments */
    for (int t = lt_x + 10; t < lt_x + tw; t += 12)
        vita2d_draw_line(t, lt_y + 2, t, lt_y + th - 2, col_tread);
    /* Track wheels (front/rear) */
    draw_circle(lt_x + 12, lt_y + th / 2, 10, col_body);
    draw_circle(lt_x + tw - 12, lt_y + th / 2, 10, col_body);

    /* Right track */
    int rt_x = rx + track_gap / 2 - tw / 2;
    int rt_y = lt_y;
    vita2d_draw_rectangle(rt_x, rt_y, tw, th, col_track);
    draw_outline_rect(rt_x, rt_y, tw, th, col_body);
    for (int t = rt_x + 10; t < rt_x + tw; t += 12)
        vita2d_draw_line(t, rt_y + 2, t, rt_y + th - 2, col_tread);
    draw_circle(rt_x + 12, rt_y + th / 2, 10, col_body);
    draw_circle(rt_x + tw - 12, rt_y + th / 2, 10, col_body);

    /* --- Chassis body --- */
    int bw = 100, bh = 36;
    int bx = rx - bw / 2, by = ry - 20;
    vita2d_draw_rectangle(bx, by, bw, bh, col_dark);
    draw_outline_rect(bx, by, bw, bh, col_body);
    /* Body detail lines */
    vita2d_draw_line(bx + 8, by + bh / 2, bx + bw - 8, by + bh / 2, col_tread);
    vita2d_draw_line(bx + 4, by + 4, bx + 4, by + bh - 4, col_body);
    vita2d_draw_line(bx + bw - 4, by + 4, bx + bw - 4, by + bh - 4, col_body);

    /* --- Camera turret --- */
    int cam_y = by - 28;
    /* Mast */
    vita2d_draw_rectangle(rx - 3, cam_y + 14, 6, 16, col_body);
    /* Turret housing */
    vita2d_draw_rectangle(rx - 20, cam_y, 40, 18, col_dark);
    draw_outline_rect(rx - 20, cam_y, 40, 18, col_bright);
    /* Camera lens */
    draw_circle(rx, cam_y + 9, 8, col_cyan);
    draw_filled_circle(rx, cam_y + 9, 4, col_cyan);
    /* Lens highlight */
    draw_filled_circle(rx - 2, cam_y + 7, 1, RGBA8(180, 240, 255, 255));

    /* --- Antenna --- */
    int ant_base_x = rx + 22, ant_base_y = cam_y + 2;
    int ant_tip_x = rx + 48, ant_tip_y = cam_y - 40;
    /* Antenna mast */
    vita2d_draw_line(ant_base_x, ant_base_y, ant_tip_x, ant_tip_y, col_bright);
    vita2d_draw_line(ant_base_x + 1, ant_base_y, ant_tip_x + 1, ant_tip_y, col_bright);
    /* Antenna glow */
    draw_filled_circle(ant_tip_x, ant_tip_y, 4, col_cyan);
    draw_circle(ant_tip_x, ant_tip_y, 7, RGBA8(0, 200, 255, 80));

    /* --- Signal arcs from antenna --- */
    for (int s = 0; s < 3; s++) {
        int sr = 12 + s * 8;
        float start_a = -1.2f, end_a = -0.3f;
        float step = (end_a - start_a) / 16;
        float px0 = ant_tip_x + sr * cosf(start_a);
        float py0 = ant_tip_y + sr * sinf(start_a);
        unsigned int arc_col = RGBA8(0, 200, 255, 160 - s * 40);
        for (int i = 1; i <= 16; i++) {
            float a = start_a + i * step;
            float px1 = ant_tip_x + sr * cosf(a);
            float py1 = ant_tip_y + sr * sinf(a);
            vita2d_draw_line(px0, py0, px1, py1, arc_col);
            px0 = px1;
            py0 = py1;
        }
    }

    /* --- Accent glow under chassis --- */
    vita2d_draw_line(bx + 10, by + bh + 1, bx + bw - 10, by + bh + 1,
                     RGBA8(0, 150, 255, 60));

    /* === Text === */
    if (pgf_font) {
        /* "PS VITA" header */
        const char* ps_txt = "PS VITA";
        int pw = vita2d_pgf_text_width(pgf_font, 1.0f, ps_txt);
        vita2d_pgf_draw_text(pgf_font, 480 - pw / 2, 300 + PGF_BASELINE_OFFSET,
                             col_sub, 1.0f, ps_txt);

        /* "ROBOT CONSOLE" title */
        const char* rc_txt = "ROBOT CONSOLE";
        int rw = vita2d_pgf_text_width(pgf_font, 1.0f, rc_txt);
        vita2d_pgf_draw_text(pgf_font, 480 - rw / 2, 325 + PGF_BASELINE_OFFSET,
                             col_title, 1.0f, rc_txt);

        /* Version */
        char ver_buf[64];
        snprintf(ver_buf, sizeof(ver_buf), "v%s", APP_VERSION);
        int vw = vita2d_pgf_text_width(pgf_font, 1.0f, ver_buf);
        vita2d_pgf_draw_text(pgf_font, 480 - vw / 2, 350 + PGF_BASELINE_OFFSET,
                             col_sub, 1.0f, ver_buf);
    }

    /* Decorative line under title */
    vita2d_draw_line(380, 375, 580, 375, RGBA8(30, 60, 120, 255));

    /* Status text at bottom */
    if (status_text && pgf_font) {
        int sw = vita2d_pgf_text_width(pgf_font, 1.0f, status_text);
        vita2d_pgf_draw_text(pgf_font, 480 - sw / 2, 420 + PGF_BASELINE_OFFSET,
                             COL_AMBER, 1.0f, status_text);
    }

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void ui_save_screenshot(void) {
    screenshot_pending = 1;
    screenshot_flash_start = get_ticks_ms();
    feedback_tone_screenshot();
}

/* Find next available screenshot number by scanning directory */
static void screenshot_find_next(void) {
    screenshot_count = 0;
    SceUID dir = sceIoDopen("ux0:data/RobotConsole/screenshots");
    if (dir >= 0) {
        SceIoDirent entry;
        while (sceIoDread(dir, &entry) > 0) {
            int num = 0;
            if (sscanf(entry.d_name, "shot_%d.png", &num) == 1) {
                if (num >= screenshot_count)
                    screenshot_count = num + 1;
            }
        }
        sceIoDclose(dir);
    }
}

/* libpng write callback using sceIo */
static void png_sceio_write(png_structp png_ptr, png_bytep data, png_size_t length) {
    SceUID *fdp = (SceUID*)png_get_io_ptr(png_ptr);
    sceIoWrite(*fdp, data, length);
}

static void png_sceio_flush(png_structp png_ptr) {
    (void)png_ptr;
}

/* Actually write screenshot — called from main loop between end_drawing and swap */
static void do_save_screenshot(void) {
    sceIoMkdir("ux0:data/RobotConsole", 0777);
    sceIoMkdir("ux0:data/RobotConsole/screenshots", 0777);

    if (screenshot_count < 0)
        screenshot_find_next();

    /* Query actual framebuffer parameters from the system */
    SceDisplayFrameBuf fb_info;
    memset(&fb_info, 0, sizeof(fb_info));
    fb_info.size = sizeof(SceDisplayFrameBuf);

    if (sceDisplayGetFrameBuf(&fb_info, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0)
        return;
    if (!fb_info.base || fb_info.width == 0)
        return;

    int w = fb_info.width;          /* 960 */
    int h = fb_info.height;         /* 544 */
    int stride = fb_info.pitch;     /* actual stride in pixels (may be 960 or 1024) */

    char path[128];
    snprintf(path, sizeof(path), "ux0:data/RobotConsole/screenshots/shot_%04d.png", screenshot_count++);

    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) { sceIoClose(fd); return; }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_write_struct(&png_ptr, NULL); sceIoClose(fd); return; }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        sceIoClose(fd);
        return;
    }

    png_set_write_fn(png_ptr, &fd, png_sceio_write, png_sceio_flush);
    png_set_IHDR(png_ptr, info_ptr, w, h, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_compression_level(png_ptr, 1);  /* fast compression */
    png_write_info(png_ptr, info_ptr);

    /* Convert framebuffer ABGR to RGB rows */
    uint32_t* pixels = (uint32_t*)fb_info.base;
    uint8_t* row_buf = malloc(w * 3);
    if (row_buf) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint32_t px = pixels[y * stride + x];
                /* Display format A8B8G8R8: little-endian uint32_t
                   bits  0-7  = R,  8-15 = G,  16-23 = B */
                row_buf[x * 3 + 0] = (px >> 0)  & 0xFF; /* R */
                row_buf[x * 3 + 1] = (px >> 8)  & 0xFF; /* G */
                row_buf[x * 3 + 2] = (px >> 16) & 0xFF; /* B */
            }
            png_write_row(png_ptr, row_buf);
        }
        free(row_buf);
    }

    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    sceIoClose(fd);
    log_write("[UI] Screenshot saved: %s (stride=%d)", path, stride);
}

static void draw_no_signal(int ax, int ay, int aw, int ah) {
    /* Dark background */
    vita2d_draw_rectangle(ax, ay, aw, ah, RGBA8(20, 20, 28, 255));

    /* Crosshatch pattern */
    unsigned int grid_col = RGBA8(30, 30, 40, 255);
    for (int y = ay; y < ay + ah; y += 16)
        vita2d_draw_line(ax, y, ax + aw, y, grid_col);
    for (int x = ax; x < ax + aw; x += 16)
        vita2d_draw_line(x, ay, x, ay + ah, grid_col);

    /* "NO SIGNAL" text in center */
    int cx = ax + aw / 2;
    int cy = ay + ah / 2;
    const char* msg = "NO SIGNAL";
    int tw = measure_text(msg);
    draw_text(cx - tw / 2, cy - 10, msg, COL_DIM);

    /* Blinking dot */
    uint32_t phase = (get_ticks_ms() / 500) % 2;
    if (phase) {
        draw_filled_circle(cx, cy + 20, 4, COL_RED);
    }
}

int ui_robot_name_tapped(void) {
    int val = robot_name_tap;
    robot_name_tap = 0;
    return val;
}

static void check_touch_taps(void) {
    TouchInput touch = input_get_touch();

    if (touch_was_active && !touch.active) {
        if (touch_tap_x >= 10 && touch_tap_x <= 170 && touch_tap_y <= TOP_BAR_H) {
            robot_name_tap = 1;
        }
    }

    if (touch.active) {
        touch_tap_x = touch.x;
        touch_tap_y = touch.y;
    }
    touch_was_active = touch.active;
}

static void draw_connection_indicator(int state, int cx, int cy, int r) {
    unsigned int c;
    if (state == NETWORK_STATUS_OK)        c = COL_GREEN;
    else if (state == NETWORK_STATUS_SLOW) c = COL_AMBER;
    else                                   c = COL_RED;
    draw_filled_circle(cx, cy, r, c);
}

static void draw_estop_flash(int active) {
    if (!active) {
        estop_flash_start = 0;
        return;
    }
    uint32_t now = get_ticks_ms();
    if (estop_flash_start == 0) estop_flash_start = now;

    /* Breathing pulse: sinusoidal alpha between 40 and 140 over ~1.5s period */
    float t = (float)((now - estop_flash_start) % 1500) / 1500.0f;
    float pulse = sinf(t * 2.0f * 3.14159265f);
    int alpha = 90 + (int)(pulse * 50.0f);  /* 40..140 */
    vita2d_draw_rectangle(0, TOP_BAR_H, SCREEN_W, CENTER_H, RGBA8(180, 0, 0, alpha));

    /* E-STOP label */
    const char* etxt = "E-STOP ACTIVE";
    int ew = measure_text(etxt);
    draw_text(CENTER_X_MID - ew / 2, TOP_BAR_H + 10, etxt, COL_RED);
}

/* === HUD Element Functions === */

static void draw_reticle(float cam_pan, float cam_tilt) {
    int cx = CENTER_X_MID;
    int cy = CENTER_Y_MID;

    draw_circle_thick(cx, cy, RETICLE_R_OUTER, 2, COL_GREEN);
    draw_circle_thick(cx, cy, RETICLE_R_INNER, 2, COL_GREEN_DIM);

    /* Crosshair lines with center gap */
    vita2d_draw_line(cx + RETICLE_GAP, cy, cx + RETICLE_LINE_LEN, cy, COL_GREEN);
    vita2d_draw_line(cx - RETICLE_GAP, cy, cx - RETICLE_LINE_LEN, cy, COL_GREEN);
    vita2d_draw_line(cx, cy - RETICLE_GAP, cx, cy - RETICLE_LINE_LEN, COL_GREEN);
    vita2d_draw_line(cx, cy + RETICLE_GAP, cx, cy + RETICLE_LINE_LEN, COL_GREEN);

    /* Cardinal tick marks */
    int ti = RETICLE_R_OUTER;
    int to = RETICLE_R_OUTER + TICK_LEN;
    vita2d_draw_line(cx + ti, cy, cx + to, cy, COL_GREEN);
    vita2d_draw_line(cx - ti, cy, cx - to, cy, COL_GREEN);
    vita2d_draw_line(cx, cy - ti, cx, cy - to, COL_GREEN);
    vita2d_draw_line(cx, cy + ti, cx, cy + to, COL_GREEN);

    /* Diagonal ticks */
    int di = (int)(RETICLE_R_OUTER * 0.707f);
    int do_ = (int)((RETICLE_R_OUTER + TICK_LEN / 2) * 0.707f);
    vita2d_draw_line(cx + di, cy - di, cx + do_, cy - do_, COL_GREEN);
    vita2d_draw_line(cx - di, cy - di, cx - do_, cy - do_, COL_GREEN);
    vita2d_draw_line(cx + di, cy + di, cx + do_, cy + do_, COL_GREEN);
    vita2d_draw_line(cx - di, cy + di, cx - do_, cy + do_, COL_GREEN);

    /* Deadzone shaded zone — subtle dark tint inside deadzone boundary */
    int dz_r = (int)(RETICLE_R_INNER * 24.0f / 128.0f);
    draw_filled_circle(cx, cy, dz_r, RGBA8(10, 10, 18, 100));

    /* Center dot */
    draw_filled_circle(cx, cy, RETICLE_DOT_R, COL_GREEN_BRIGHT);

    /* Pan/tilt indicator */
    int pan_px  = (int)(cam_pan  * (RETICLE_R_INNER - 6));
    int tilt_px = (int)(-cam_tilt * (RETICLE_R_INNER - 6));
    if (pan_px != 0 || tilt_px != 0)
        draw_filled_circle(cx + pan_px, cy + tilt_px, 3, COL_GREEN_BRIGHT);
}

static void draw_lin_slider(float linear) {
    int track_x = LIN_X - LIN_WIDTH / 2;
    int track_y = LIN_Y_TOP;

    draw_text(LIN_X - 12, track_y - 22, "LIN", COL_DIM);

    /* Track background + border */
    vita2d_draw_rectangle(track_x, track_y, LIN_WIDTH, LIN_HEIGHT, COL_TRACK);
    draw_outline_rect(track_x, track_y, LIN_WIDTH, LIN_HEIGHT, COL_TRACK_BORDER);

    /* Center zero line */
    int center_y = track_y + LIN_HEIGHT / 2;
    vita2d_draw_line(track_x, center_y, track_x + LIN_WIDTH, center_y, COL_DIM);

    /* Fill bar */
    int fill = (int)(linear * (LIN_HEIGHT / 2));
    if (fill != 0) {
        if (fill > 0) {
            vita2d_draw_rectangle(track_x + 2, center_y - fill, LIN_WIDTH - 4, fill, COL_GREEN);
        } else {
            vita2d_draw_rectangle(track_x + 2, center_y, LIN_WIDTH - 4, -fill, COL_GREEN);
        }
    }
}

static void draw_ang_slider(float angular) {
    int track_x = ANG_X_LEFT;
    int track_y = ANG_Y - ANG_HEIGHT / 2;

    int lw = measure_text("ANG");
    draw_text(CENTER_X_MID - lw / 2, track_y + ANG_HEIGHT + 6, "ANG", COL_DIM);

    vita2d_draw_rectangle(track_x, track_y, ANG_WIDTH, ANG_HEIGHT, COL_TRACK);
    draw_outline_rect(track_x, track_y, ANG_WIDTH, ANG_HEIGHT, COL_TRACK_BORDER);

    int center_x = track_x + ANG_WIDTH / 2;
    vita2d_draw_line(center_x, track_y, center_x, track_y + ANG_HEIGHT, COL_DIM);

    int fill = (int)(angular * (ANG_WIDTH / 2));
    if (fill != 0) {
        if (fill > 0) {
            vita2d_draw_rectangle(center_x, track_y + 2, fill, ANG_HEIGHT - 4, COL_GREEN);
        } else {
            vita2d_draw_rectangle(center_x + fill, track_y + 2, -fill, ANG_HEIGHT - 4, COL_GREEN);
        }
    }
}

static void draw_lift_buttons(int lifter_state) {
    draw_text(LIFT_X - 16, LIFT_UP_Y - 30, "LIFT", COL_DIM);

    /* Up button */
    int ux = LIFT_X - LIFT_BTN_W / 2;
    if (lifter_state > 0) {
        vita2d_draw_rectangle(ux, LIFT_UP_Y, LIFT_BTN_W, LIFT_BTN_H, COL_GREEN);
        draw_text(LIFT_X - 4, LIFT_UP_Y + 10, "^", COL_DARK_TEXT);
    } else {
        draw_outline_rect(ux, LIFT_UP_Y, LIFT_BTN_W, LIFT_BTN_H, COL_TRACK_BORDER);
        draw_text(LIFT_X - 4, LIFT_UP_Y + 10, "^", COL_DIM);
    }

    /* Down button */
    if (lifter_state < 0) {
        vita2d_draw_rectangle(ux, LIFT_DOWN_Y, LIFT_BTN_W, LIFT_BTN_H, COL_AMBER);
        draw_text(LIFT_X - 4, LIFT_DOWN_Y + 10, "v", COL_DARK_TEXT);
    } else {
        draw_outline_rect(ux, LIFT_DOWN_Y, LIFT_BTN_W, LIFT_BTN_H, COL_TRACK_BORDER);
        draw_text(LIFT_X - 4, LIFT_DOWN_Y + 10, "v", COL_DIM);
    }
}

static void draw_top_bar(const RobotStatus* status, const char* robot_ip) {
    vita2d_draw_rectangle(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, TOP_BAR_H - 1, SCREEN_W, TOP_BAR_H - 1, COL_GREEN_DIM);

    int text_y = (TOP_BAR_H - FONT_SIZE) / 2;
    int conn_state = status ? status->connection_state : NETWORK_STATUS_FAIL;

    draw_connection_indicator(conn_state, 19, TOP_BAR_H / 2, 7);

    char robot_name[16];
    snprintf(robot_name, sizeof(robot_name), "ROBOT_%02d", robot_get_active_index() + 1);
    draw_text(34, text_y, robot_name, COL_WHITE);
    draw_text(145, text_y, "(w)", COL_DIM);
    if (robot_ip)
        draw_text(180, text_y, robot_ip, COL_DIM);

    if (!status) return;

    /* Ping */
    unsigned int ping_color = COL_GREEN;
    if (status->ping_ms > 150 || status->connection_state == NETWORK_STATUS_SLOW)
        ping_color = COL_AMBER;
    if (status->ping_ms < 0 || status->connection_state == NETWORK_STATUS_FAIL)
        ping_color = COL_RED;

    char ping_buf[16];
    if (status->ping_ms >= 0)
        snprintf(ping_buf, sizeof(ping_buf), "%dMS", status->ping_ms);
    else
        snprintf(ping_buf, sizeof(ping_buf), "--");
    draw_text(565, text_y, ">", ping_color);
    draw_text(580, text_y, ping_buf, ping_color);

    /* Battery */
    int batt = (status->battery >= 0 && status->battery <= 100) ? status->battery : 0;
    unsigned int batt_color = COL_GREEN;
    if (batt <= 20) batt_color = COL_RED;
    else if (batt <= 40) batt_color = COL_AMBER;

    int batt_x = 660, batt_y = TOP_BAR_H / 2 - 8;
    draw_outline_rect(batt_x, batt_y, 30, 16, batt_color);
    vita2d_draw_rectangle(batt_x + 30, batt_y + 4, 3, 8, batt_color);
    int fill_w = (28 * batt) / 100;
    if (fill_w > 0)
        vita2d_draw_rectangle(batt_x + 1, batt_y + 1, fill_w, 14, batt_color);

    char batt_buf[8];
    snprintf(batt_buf, sizeof(batt_buf), "%d%%", batt);
    draw_text(700, text_y, batt_buf, batt_color);

    /* WiFi signal bars (RSSI-based) */
    {
        int rssi_pct = network_get_rssi();  /* 0-100% */
        int bars_filled;
        unsigned int sig_col;

        if (conn_state == NETWORK_STATUS_FAIL || rssi_pct == 0) {
            bars_filled = 0;
            sig_col = COL_RED;
        } else if (rssi_pct < 30) {
            bars_filled = 1;
            sig_col = COL_RED;
        } else if (rssi_pct < 60) {
            bars_filled = 2;
            sig_col = COL_AMBER;
        } else if (rssi_pct < 80) {
            bars_filled = 3;
            sig_col = COL_GREEN;
        } else {
            bars_filled = 4;
            sig_col = COL_GREEN;
        }

        int sig_x = 750, sig_base = TOP_BAR_H / 2 + 7;
        int bar_heights[] = {5, 8, 11, 14};
        for (int i = 0; i < 4; i++) {
            unsigned int bc = (i < bars_filled) ? sig_col : COL_TRACK;
            vita2d_draw_rectangle(sig_x + i * 6, sig_base - bar_heights[i], 4, bar_heights[i], bc);
        }
    }

    /* LINK badge */
    const char* link_text;
    unsigned int badge_col;
    if (conn_state == NETWORK_STATUS_OK) {
        link_text = "LINK_ACTIVE"; badge_col = COL_GREEN;
    } else if (conn_state == NETWORK_STATUS_SLOW) {
        link_text = "LINK_SLOW"; badge_col = COL_AMBER;
    } else {
        link_text = "LINK_FAIL"; badge_col = COL_RED;
    }
    int badge_w = measure_text(link_text) + 16;
    int badge_h = 22;
    int badge_x = SCREEN_W - badge_w - 10;
    int badge_y2 = (TOP_BAR_H - badge_h) / 2;
    draw_rounded_rect_filled(badge_x, badge_y2, badge_w, badge_h, 6, badge_col);
    draw_text(badge_x + 8, badge_y2 + 2, link_text, COL_DARK_TEXT);

    /* Connection timeout (show seconds since last OK when failing) */
    if (conn_state == NETWORK_STATUS_FAIL) {
        uint32_t age = network_get_last_ok_age_ms();
        if (age > 0) {
            char age_buf[24];
            snprintf(age_buf, sizeof(age_buf), "LAST OK: %ds", age / 1000);
            int aw = measure_text(age_buf);
            draw_text(SCREEN_W - aw - 10, TOP_BAR_H - 20, age_buf, COL_RED);
        }
    }
}

static void draw_btn_label(int x, int y, const char* label, unsigned int col) {
    draw_outline_rect(x, y, 18, 18, col);
    int tw = measure_text(label);
    draw_text(x + (18 - tw) / 2, y + 1, label, col);
}

static void draw_btn_triangle(int cx, int cy, int size, unsigned int col) {
    int top_y = cy - size;
    int bot_y = cy + size / 2;
    int left_x = cx - size;
    int right_x = cx + size;
    vita2d_draw_line(cx, top_y, left_x, bot_y, col);
    vita2d_draw_line(left_x, bot_y, right_x, bot_y, col);
    vita2d_draw_line(right_x, bot_y, cx, top_y, col);
}

static void draw_btn_circle(int cx, int cy, int r, unsigned int col) {
    draw_circle(cx, cy, r, col);
}

/* === IME Dialog Helpers === */

static void utf8_to_utf16(const char* src, SceWChar16* dst, int dst_len) {
    int i = 0, j = 0;
    while (src[i] && j < dst_len - 1) {
        dst[j++] = (SceWChar16)(unsigned char)src[i++];
    }
    dst[j] = 0;
}

static void utf16_to_utf8(const SceWChar16* src, char* dst, int dst_len) {
    int i = 0, j = 0;
    while (src[i] && j < dst_len - 1) {
        dst[j++] = (char)(src[i++] & 0xFF);
    }
    dst[j] = '\0';
}

static void config_open_ime(int field) {
    const char* current_val = "";
    const char* title = "";

    int idx = config_selected_robot;
    switch (field) {
    case 0:
        current_val = robot_get_ip_by_index(idx);
        title = "Robot IP (Control)";
        break;
    case 1:
        current_val = robot_get_camera_host(idx);
        title = "Camera Host (or blank=Robot IP)";
        break;
    case 2: {
        static char port_buf[16];
        snprintf(port_buf, sizeof(port_buf), "%d", robot_get_camera_port(idx));
        current_val = port_buf;
        title = "Camera Port";
        break;
    }
    case 3:
        current_val = robot_get_stream_endpoint(idx);
        title = "Stream Endpoint";
        break;
    }

    memset(ime_input_buf, 0, sizeof(ime_input_buf));
    memset(ime_initial_buf, 0, sizeof(ime_initial_buf));
    utf8_to_utf16(current_val, ime_initial_buf, 128);
    memcpy(ime_input_buf, ime_initial_buf, sizeof(ime_initial_buf));
    strncpy(ime_title_buf, title, sizeof(ime_title_buf) - 1);

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0;
    param.languagesForced = SCE_FALSE;
    param.type = SCE_IME_TYPE_DEFAULT;
    if (field == 2)
        param.type = SCE_IME_TYPE_NUMBER;
    else if (field == 0 || field == 1)
        param.type = SCE_IME_TYPE_URL;
    param.option = 0;

    SceWChar16 title16[64];
    utf8_to_utf16(title, title16, 64);
    param.title = title16;
    param.maxTextLength = (field == 2) ? 8 : 63;
    param.initialText = ime_initial_buf;
    param.inputTextBuffer = ime_input_buf;

    int ret = sceImeDialogInit(&param);
    if (ret < 0) {
        printf("[UI] IME dialog init failed: 0x%08x\n", ret);
        ime_active = 0;
    } else {
        ime_active = 1;
        config_editing = 1;
    }
}

static void config_update_ime(void) {
    if (!ime_active) return;

    SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
        SceImeDialogResult result;
        memset(&result, 0, sizeof(result));
        sceImeDialogGetResult(&result);

        if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
            char utf8_buf[128];
            utf16_to_utf8(ime_input_buf, utf8_buf, sizeof(utf8_buf));

            int idx = config_selected_robot;
            int valid = 1;

            switch (config_selected_field) {
            case 0: /* Control IP */
                if (validate_ip(utf8_buf)) {
                    robot_set_ip_by_index(idx, utf8_buf);
                    config_set_validation("IP SAVED", COL_GREEN);
                } else {
                    config_set_validation("INVALID IP (x.x.x.x)", COL_RED);
                    feedback_tone_error();
                    valid = 0;
                }
                break;
            case 1: /* Camera Host */
                if (utf8_buf[0] == '\0' || validate_ip(utf8_buf)) {
                    robot_set_camera_host(idx, utf8_buf);
                    config_set_validation(utf8_buf[0] ? "CAMERA HOST SAVED" : "CAMERA HOST CLEARED", COL_GREEN);
                } else {
                    config_set_validation("INVALID HOST (x.x.x.x)", COL_RED);
                    feedback_tone_error();
                    valid = 0;
                }
                break;
            case 2: { /* Port */
                int port = atoi(utf8_buf);
                if (port >= 1 && port <= 65535) {
                    robot_set_camera_port(idx, port);
                    config_set_validation("PORT SAVED", COL_GREEN);
                } else {
                    config_set_validation("INVALID PORT (1-65535)", COL_RED);
                    feedback_tone_error();
                    valid = 0;
                }
                break;
            }
            case 3: /* Endpoint */
                if (utf8_buf[0] == '/' || utf8_buf[0] == '\0') {
                    robot_set_stream_endpoint(idx, utf8_buf);
                    config_set_validation("ENDPOINT SAVED", COL_GREEN);
                } else {
                    /* Auto-fix: prepend / if missing */
                    char fixed[130];
                    snprintf(fixed, sizeof(fixed), "/%s", utf8_buf);
                    robot_set_stream_endpoint(idx, fixed);
                    config_set_validation("ENDPOINT SAVED (/ ADDED)", COL_AMBER);
                }
                break;
            }
            printf("[UI] IME input %s: field=%d val=%s\n",
                   valid ? "accepted" : "rejected", config_selected_field, utf8_buf);
        }

        sceImeDialogTerm();
        ime_active = 0;
        config_editing = 0;
    }
}

static void config_handle_input(void) {
    if (ime_active) return;

    unsigned int pressed = input_get_buttons_pressed();
    int robot_count = robot_get_count();

    if (pressed & SCE_CTRL_RIGHT)
        config_selected_robot = (config_selected_robot + 1) % robot_count;
    if (pressed & SCE_CTRL_LEFT)
        config_selected_robot = (config_selected_robot - 1 + robot_count) % robot_count;
    if (pressed & SCE_CTRL_DOWN)
        config_selected_field = (config_selected_field + 1) % 5;
    if (pressed & SCE_CTRL_UP)
        config_selected_field = (config_selected_field + 4) % 5;
    if (pressed & SCE_CTRL_CROSS) {
        if (config_selected_field == 4) {
            /* Toggle UI sounds mute */
            int muted = robot_get_ui_sounds_muted();
            robot_set_ui_sounds_muted(!muted);
            feedback_set_ui_mute(!muted);
            config_set_validation(!muted ? "UI SOUNDS OFF" : "UI SOUNDS ON",
                                  !muted ? COL_RED : COL_GREEN);
        } else {
            config_open_ime(config_selected_field);
        }
    }

    /* Triangle: test connection to selected robot */
    if (pressed & SCE_CTRL_TRIANGLE) {
        int idx = config_selected_robot;
        const char* ip = robot_get_ip_by_index(idx);
        config_test_result = network_test_connection(ip);
        config_test_time = get_ticks_ms();
    }

    /* Circle: add new robot */
    if (pressed & SCE_CTRL_CIRCLE) {
        int new_idx = robot_add();
        if (new_idx >= 0) {
            config_selected_robot = new_idx;
            config_set_validation("ROBOT ADDED", COL_GREEN);
            log_write("[CONFIG] Added robot %d", new_idx + 1);
        } else {
            config_set_validation("MAX ROBOTS REACHED", COL_RED);
            feedback_tone_error();
        }
    }

    /* Square: remove selected robot (min 1) */
    if (pressed & SCE_CTRL_SQUARE) {
        if (robot_get_count() > 1) {
            int removed = config_selected_robot;
            if (robot_remove(removed) == 0) {
                if (config_selected_robot >= robot_get_count())
                    config_selected_robot = robot_get_count() - 1;
                config_set_validation("ROBOT REMOVED", COL_AMBER);
                feedback_tone_delete();
                log_write("[CONFIG] Removed robot %d", removed + 1);
            }
        } else {
            config_set_validation("NEED AT LEAST 1 ROBOT", COL_RED);
            feedback_tone_error();
        }
    }
}

/* === Config Screen Drawing === */

static void draw_config_screen(void) {
    int robot_count = robot_get_count();
    int idx = config_selected_robot;

    /* Top bar */
    vita2d_draw_rectangle(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, TOP_BAR_H - 1, SCREEN_W, TOP_BAR_H - 1, COL_GREEN_DIM);

    draw_text(20, (TOP_BAR_H - FONT_SIZE) / 2, "ROBOT CONFIGURATION", COL_GREEN_BRIGHT);

    {
        char ver_buf[32];
        snprintf(ver_buf, sizeof(ver_buf), "v%s", APP_VERSION);
        int vw = measure_text(ver_buf);
        draw_text(SCREEN_W - vw - 20, (TOP_BAR_H - FONT_SIZE) / 2, ver_buf, COL_DIM);
    }

    /* Robot tabs */
    int tab_y = TOP_BAR_H + 15;
    int tab_w = 200;
    int tab_spacing = 20;
    int tabs_total_w = robot_count * tab_w + (robot_count - 1) * tab_spacing;
    int tab_start_x = (SCREEN_W - tabs_total_w) / 2;

    for (int i = 0; i < robot_count; i++) {
        int tx = tab_start_x + i * (tab_w + tab_spacing);
        int is_sel = (i == idx);

        if (is_sel) {
            vita2d_draw_rectangle(tx, tab_y, tab_w, 32, RGBA8(0, 200, 80, 40));
            draw_outline_rect(tx, tab_y, tab_w, 32, COL_GREEN);
        } else {
            draw_outline_rect(tx, tab_y, tab_w, 32, COL_TRACK_BORDER);
        }

        char tab_label[16];
        snprintf(tab_label, sizeof(tab_label), "ROBOT %d", i + 1);
        int lw = measure_text(tab_label);
        draw_text(tx + (tab_w - lw) / 2, tab_y + 7,
                  tab_label, is_sel ? COL_GREEN_BRIGHT : COL_DIM);
    }

    /* Underline selected tab */
    {
        int sel_x = tab_start_x + idx * (tab_w + tab_spacing);
        vita2d_draw_line(sel_x, tab_y + 33, sel_x + tab_w, tab_y + 33, COL_GREEN);
    }

    /* Fields */
    const char* field_labels[] = { "CONTROL IP", "CAMERA HOST", "STREAM PORT", "STREAM ENDPOINT" };
    int field_y_start = tab_y + 50;
    int field_spacing = 62;
    int field_x = 180;
    int field_w = 500;
    int field_h = 26;

    for (int f = 0; f < 4; f++) {
        int fy = field_y_start + f * field_spacing;
        int is_sel = (f == config_selected_field);

        draw_text(field_x, fy, field_labels[f], is_sel ? COL_GREEN : COL_DIM);

        vita2d_draw_rectangle(field_x, fy + 18, field_w, field_h, COL_TRACK);
        unsigned int border_col = is_sel ? COL_GREEN : COL_TRACK_BORDER;
        draw_outline_rect(field_x, fy + 18, field_w, field_h, border_col);

        const char* val = "";
        static char port_str[16];
        switch (f) {
        case 0: val = robot_get_ip_by_index(idx); break;
        case 1: val = robot_get_camera_host(idx); break;
        case 2:
            snprintf(port_str, sizeof(port_str), "%d", robot_get_camera_port(idx));
            val = port_str;
            break;
        case 3: val = robot_get_stream_endpoint(idx); break;
        }

        if (f == 1 && (!val || val[0] == '\0')) {
            draw_text(field_x + 8, fy + 21, "(SAME AS CONTROL IP)", COL_TRACK_BORDER);
        } else {
            unsigned int val_col = is_sel ? COL_WHITE : COL_DIM;
            draw_text(field_x + 8, fy + 21, val, val_col);
        }

        if (is_sel)
            draw_text(field_x - 25, fy + 21, ">", COL_GREEN_BRIGHT);
    }

    /* UI Sounds toggle (field 4) */
    {
        int fy = field_y_start + 4 * field_spacing;
        int is_sel = (config_selected_field == 4);
        int muted = robot_get_ui_sounds_muted();
        draw_text(field_x, fy, "UI SOUNDS", is_sel ? COL_GREEN : COL_DIM);
        const char* state_txt = muted ? "OFF" : "ON";
        unsigned int state_col = muted ? COL_RED : COL_GREEN;
        vita2d_draw_rectangle(field_x, fy + 18, field_w, field_h, COL_TRACK);
        draw_outline_rect(field_x, fy + 18, field_w, field_h,
                          is_sel ? COL_GREEN : COL_TRACK_BORDER);
        draw_text(field_x + 8, fy + 21, state_txt, state_col);
        if (is_sel)
            draw_text(field_x - 25, fy + 21, ">", COL_GREEN_BRIGHT);
    }

    /* URL preview */
    int preview_y = field_y_start + 4 * field_spacing + 5;
    const char* cam_host = robot_get_camera_host(idx);
    const char* preview_host = (cam_host[0] != '\0') ? cam_host : robot_get_ip_by_index(idx);
    const char* ep = robot_get_stream_endpoint(idx);
    char url_buf[192];

    if (ep[0] != '/' && ep[0] != '\0')
        snprintf(url_buf, sizeof(url_buf), "http://%s:%d/%s", preview_host, robot_get_camera_port(idx), ep);
    else
        snprintf(url_buf, sizeof(url_buf), "http://%s:%d%s", preview_host, robot_get_camera_port(idx), ep);

    draw_text(field_x, preview_y, "STREAM URL:", COL_DIM);
    draw_text(field_x, preview_y + 20, url_buf, COL_GREEN);

    /* Connection test result (fades after 3s) */
    if (config_test_time > 0 && (get_ticks_ms() - config_test_time) < 3000) {
        const char* rtxt = config_test_result ? "CONNECTION OK" : "CONNECTION FAILED";
        unsigned int rcol = config_test_result ? COL_GREEN : COL_RED;
        draw_text(field_x, preview_y + 42, rtxt, rcol);
    }

    /* Validation feedback (fades after 2s) */
    if (config_validation_time > 0 && (get_ticks_ms() - config_validation_time) < 2000) {
        draw_text(field_x, preview_y + 62, config_validation_msg, config_validation_col);
    }

    /* Bottom help bar */
    vita2d_draw_rectangle(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, BOTTOM_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, SCREEN_H - BOTTOM_BAR_H, COL_GREEN_DIM);

    int hy = SCREEN_H - BOTTOM_BAR_H + 20;
    int hx = 20;

    draw_text(hx, hy, "D-PAD", COL_WHITE);
    hx += measure_text("D-PAD") + 8;
    draw_text(hx, hy, "NAVIGATE", COL_DIM);
    hx += measure_text("NAVIGATE") + 30;

    draw_text(hx, hy, "X", COL_PS_CROSS);
    hx += measure_text("X") + 8;
    draw_text(hx, hy, "EDIT", COL_DIM);
    hx += measure_text("EDIT") + 30;

    draw_btn_triangle(hx + 8, hy + 9, 7, COL_PS_TRIANGLE);
    hx += 22;
    draw_text(hx, hy, "PING", COL_DIM);
    hx += measure_text("PING") + 20;

    draw_btn_circle(hx + 8, hy + 9, 7, COL_PS_CIRCLE);
    hx += 22;
    draw_text(hx, hy, "ADD", COL_DIM);
    hx += measure_text("ADD") + 12;

    draw_btn_label(hx, hy - 2, "S", COL_PS_SQUARE);
    hx += 20;
    draw_text(hx, hy, "DEL", COL_DIM);
    hx += measure_text("DEL") + 20;

    draw_text(hx, hy, "START", COL_WHITE);
    hx += measure_text("START") + 6;
    draw_text(hx, hy, "SAVE", COL_DIM);
    hx += measure_text("SAVE") + 20;

    draw_text(hx, hy, "SEL", COL_WHITE);
    hx += measure_text("SEL") + 6;
    draw_text(hx, hy, "HELP", COL_DIM);
}

static void draw_bottom_bar(const ControlState* cs) {
    vita2d_draw_rectangle(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, BOTTOM_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, SCREEN_H - BOTTOM_BAR_H, COL_GREEN_DIM);

    int hint_y = SCREEN_H - BOTTOM_BAR_H + 12;
    int hx = 12;

    draw_btn_label(hx, hint_y, "L", COL_WHITE);
    hx += 22;
    draw_btn_label(hx, hint_y, "R", COL_WHITE);
    hx += 24;
    draw_text(hx, hint_y + 1, "LIFTER", COL_DIM);
    hx += measure_text("LIFTER") + 24;

    draw_btn_triangle(hx + 8, hint_y + 9, 7, COL_PS_TRIANGLE);
    hx += 22;
    draw_text(hx, hint_y + 1, "ROBOT", COL_DIM);
    hx += measure_text("ROBOT") + 24;

    draw_btn_circle(hx + 8, hint_y + 9, 7, COL_PS_CIRCLE);
    hx += 22;
    draw_text(hx, hint_y + 1, "E-STOP", COL_RED);
    hx += measure_text("E-STOP") + 24;

    draw_text(hx, hint_y + 1, "START", COL_WHITE);
    hx += measure_text("START") + 8;
    draw_text(hx, hint_y + 1, "CONFIG", COL_DIM);
    hx += measure_text("CONFIG") + 24;

    draw_btn_label(hx, hint_y, "S", COL_PS_SQUARE);
    hx += 24;
    draw_text(hx, hint_y + 1, "SOUNDS", COL_DIM);

    /* Telemetry values (right side, lower row) — amber when active, green when idle */
    int telem_y = SCREEN_H - BOTTOM_BAR_H + 30;
    int any_input = (cs->linear != 0.0f || cs->angular != 0.0f ||
                     cs->cam_pan != 0.0f || cs->cam_tilt != 0.0f);
    unsigned int telem_col = any_input ? COL_AMBER : COL_GREEN;
    char telem_buf[128];
    snprintf(telem_buf, sizeof(telem_buf),
             "LIN: %+.2f   ANG: %+.2f   PAN: %+.2f   TLT: %+.2f",
             cs->linear, cs->angular, cs->cam_pan, cs->cam_tilt);
    int tw = measure_text(telem_buf);
    draw_text(SCREEN_W - tw - 12, telem_y, telem_buf, telem_col);
}

/* === Sounds Screen === */

static const char* sound_names[] = {"HORN", "ALERT", "BEEP", "SIREN", "CHIME"};
#define SOUND_COUNT 5
static int sound_selected = 0;
static uint32_t sound_play_time = 0;
static int sound_play_id = -1;

/* Button grid positions: row 1 has 3 buttons, row 2 has 2 centered */
static void sound_btn_pos(int idx, int* out_x, int* out_y) {
    int btn_w = 160, btn_h = 80, gap = 30;
    if (idx < 3) {
        /* Row 1: 3 buttons */
        int total_w = 3 * btn_w + 2 * gap;
        int start_x = (SCREEN_W - total_w) / 2;
        *out_x = start_x + idx * (btn_w + gap);
        *out_y = TOP_BAR_H + 50;
    } else {
        /* Row 2: 2 buttons centered */
        int total_w = 2 * btn_w + gap;
        int start_x = (SCREEN_W - total_w) / 2;
        *out_x = start_x + (idx - 3) * (btn_w + gap);
        *out_y = TOP_BAR_H + 50 + btn_h + 40;
    }
}

static void sounds_handle_input(void) {
    unsigned int pressed = input_get_buttons_pressed();

    /* D-pad navigation */
    if (pressed & SCE_CTRL_RIGHT) {
        if (sound_selected < 2) sound_selected++;
        else if (sound_selected == 3) sound_selected = 4;
    }
    if (pressed & SCE_CTRL_LEFT) {
        if (sound_selected > 0 && sound_selected <= 2) sound_selected--;
        else if (sound_selected == 4) sound_selected = 3;
    }
    if (pressed & SCE_CTRL_DOWN) {
        if (sound_selected < 3) {
            /* Row 1 -> Row 2: map 0,1->3  2->4 */
            sound_selected = (sound_selected < 2) ? 3 : 4;
        }
    }
    if (pressed & SCE_CTRL_UP) {
        if (sound_selected >= 3) {
            /* Row 2 -> Row 1: map 3->0  4->2 */
            sound_selected = (sound_selected == 3) ? 1 : 2;
        }
    }

    /* Cross button: play sound */
    if (pressed & SCE_CTRL_CROSS) {
        network_play_sound(sound_selected);
        sound_play_time = get_ticks_ms();
        sound_play_id = sound_selected;
        log_write("[SOUNDS] Playing sound %d: %s", sound_selected, sound_names[sound_selected]);
    }
}

static void draw_sounds_screen(void) {
    int btn_w = 160, btn_h = 80;

    /* Top bar */
    vita2d_draw_rectangle(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, TOP_BAR_H, SCREEN_W, TOP_BAR_H, COL_GREEN_DIM);
    draw_text(16, 20, "SOUND BOARD", COL_GREEN_BRIGHT);
    if (pgf_font) {
        char ver_buf[32];
        snprintf(ver_buf, sizeof(ver_buf), "v%s", APP_VERSION);
        int vw = measure_text(ver_buf);
        draw_text(SCREEN_W - vw - 16, 20, ver_buf, COL_DIM);
    }

    /* Draw 5 sound buttons */
    for (int i = 0; i < SOUND_COUNT; i++) {
        int bx, by;
        sound_btn_pos(i, &bx, &by);

        int selected = (i == sound_selected);
        unsigned int bg_col = selected ? RGBA8(25, 50, 80, 255) : RGBA8(20, 20, 30, 255);
        unsigned int border_col = selected ? COL_GREEN_BRIGHT : COL_DIM;
        unsigned int text_col = selected ? COL_WHITE : COL_DIM;

        /* Button background */
        vita2d_draw_rectangle(bx, by, btn_w, btn_h, bg_col);
        draw_outline_rect(bx, by, btn_w, btn_h, border_col);

        /* Selection indicator — thicker border */
        if (selected) {
            draw_outline_rect(bx - 1, by - 1, btn_w + 2, btn_h + 2, COL_GREEN);
        }

        /* Sound name */
        int nw = measure_text(sound_names[i]);
        draw_text(bx + (btn_w - nw) / 2, by + 16, sound_names[i], text_col);

        /* Speaker icon (simple) */
        int sx = bx + btn_w / 2 - 8;
        int sy = by + 46;
        vita2d_draw_rectangle(sx, sy, 6, 10, text_col);
        vita2d_draw_line(sx + 6, sy, sx + 14, sy - 6, text_col);
        vita2d_draw_line(sx + 6, sy + 10, sx + 14, sy + 16, text_col);
        vita2d_draw_line(sx + 14, sy - 6, sx + 14, sy + 16, text_col);
    }

    /* "Playing" feedback */
    if (sound_play_time > 0 && sound_play_id >= 0) {
        uint32_t elapsed = get_ticks_ms() - sound_play_time;
        if (elapsed < 2000) {
            int alpha = (elapsed < 1500) ? 255 : 255 - (int)((elapsed - 1500) * 255 / 500);
            if (alpha < 0) alpha = 0;
            char play_buf[64];
            snprintf(play_buf, sizeof(play_buf), "Playing: %s...", sound_names[sound_play_id]);
            int pw = measure_text(play_buf);
            unsigned int play_col = RGBA8(0, 200, 80, alpha);
            draw_text(SCREEN_W / 2 - pw / 2, TOP_BAR_H + 50 + 80 + 40 + 80 + 30, play_buf, play_col);
        } else {
            sound_play_time = 0;
        }
    }

    /* Bottom bar */
    vita2d_draw_rectangle(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, BOTTOM_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, SCREEN_H - BOTTOM_BAR_H, COL_GREEN_DIM);
    int hint_y = SCREEN_H - BOTTOM_BAR_H + 20;
    int hx = 40;

    draw_text(hx, hint_y, "D-PAD", COL_WHITE);
    hx += measure_text("D-PAD") + 8;
    draw_text(hx, hint_y, "SELECT", COL_DIM);
    hx += measure_text("SELECT") + 30;

    draw_text(hx, hint_y, "X", COL_PS_CROSS);
    hx += measure_text("X") + 8;
    draw_text(hx, hint_y, "PLAY", COL_DIM);
    hx += measure_text("PLAY") + 30;

    draw_btn_label(hx, hint_y - 1, "S", COL_PS_SQUARE);
    hx += 24;
    draw_text(hx, hint_y, "BACK", COL_DIM);
}

/* === Screenshot Gallery === */

#define GALLERY_MAX 200
static char gallery_files[GALLERY_MAX][32];  /* filenames only */
static int gallery_count = 0;
static int gallery_index = 0;
static vita2d_texture* gallery_texture = NULL;
static int gallery_loaded_index = -1;
static int gallery_delete_confirm = 0;       /* 1 = waiting for confirm press */
static uint32_t gallery_delete_confirm_time = 0;

static void gallery_scan(void) {
    gallery_count = 0;
    SceUID dir = sceIoDopen("ux0:data/RobotConsole/screenshots");
    if (dir < 0) return;

    SceIoDirent entry;
    while (sceIoDread(dir, &entry) > 0 && gallery_count < GALLERY_MAX) {
        int num = 0;
        if (sscanf(entry.d_name, "shot_%d.png", &num) == 1) {
            strncpy(gallery_files[gallery_count], entry.d_name, 31);
            gallery_files[gallery_count][31] = '\0';
            gallery_count++;
        }
    }
    sceIoDclose(dir);

    /* Simple insertion sort by filename (ascending) */
    for (int i = 1; i < gallery_count; i++) {
        char tmp[32];
        strncpy(tmp, gallery_files[i], 32);
        int j = i - 1;
        while (j >= 0 && strcmp(gallery_files[j], tmp) > 0) {
            strncpy(gallery_files[j + 1], gallery_files[j], 32);
            j--;
        }
        strncpy(gallery_files[j + 1], tmp, 32);
    }

    /* Start at most recent */
    gallery_index = (gallery_count > 0) ? gallery_count - 1 : 0;
    gallery_loaded_index = -1;
}

static void gallery_load_current(void) {
    if (gallery_count == 0) return;
    if (gallery_loaded_index == gallery_index && gallery_texture) return;

    if (gallery_texture) {
        vita2d_free_texture(gallery_texture);
        gallery_texture = NULL;
    }

    char path[128];
    snprintf(path, sizeof(path), "ux0:data/RobotConsole/screenshots/%s",
             gallery_files[gallery_index]);
    gallery_texture = vita2d_load_PNG_file(path);
    gallery_loaded_index = gallery_index;
}

static void gallery_delete_current(void) {
    if (gallery_count == 0) return;

    char path[128];
    snprintf(path, sizeof(path), "ux0:data/RobotConsole/screenshots/%s",
             gallery_files[gallery_index]);
    sceIoRemove(path);
    log_write("[GALLERY] Deleted %s", gallery_files[gallery_index]);

    /* Free current texture */
    if (gallery_texture) {
        vita2d_free_texture(gallery_texture);
        gallery_texture = NULL;
    }
    gallery_loaded_index = -1;

    /* Remove from list by shifting */
    for (int i = gallery_index; i < gallery_count - 1; i++) {
        strncpy(gallery_files[i], gallery_files[i + 1], 32);
    }
    gallery_count--;

    /* Adjust index */
    if (gallery_index >= gallery_count && gallery_count > 0)
        gallery_index = gallery_count - 1;
    if (gallery_count == 0)
        gallery_index = 0;
}

static void gallery_handle_input(void) {
    unsigned int pressed = input_get_buttons_pressed();

    /* Cancel delete confirm after 2s timeout */
    if (gallery_delete_confirm &&
        (get_ticks_ms() - gallery_delete_confirm_time) > 2000) {
        gallery_delete_confirm = 0;
    }

    if (pressed & SCE_CTRL_RIGHT) {
        if (gallery_index < gallery_count - 1) gallery_index++;
        gallery_delete_confirm = 0;
    }
    if (pressed & SCE_CTRL_LEFT) {
        if (gallery_index > 0) gallery_index--;
        gallery_delete_confirm = 0;
    }

    /* Triangle: delete with confirmation */
    if (pressed & SCE_CTRL_TRIANGLE) {
        if (gallery_count > 0) {
            if (gallery_delete_confirm) {
                gallery_delete_current();
                gallery_delete_confirm = 0;
                feedback_tone_delete();
            } else {
                gallery_delete_confirm = 1;
                gallery_delete_confirm_time = get_ticks_ms();
            }
        }
    }
}

static void gallery_cleanup(void) {
    if (gallery_texture) {
        vita2d_free_texture(gallery_texture);
        gallery_texture = NULL;
    }
    gallery_loaded_index = -1;
    gallery_delete_confirm = 0;
}

static void draw_gallery_screen(void) {
    /* Scan on first render (or when re-entering) */
    if (gallery_loaded_index < 0 && gallery_count == 0) {
        gallery_scan();
    }

    /* Top bar */
    vita2d_draw_rectangle(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, TOP_BAR_H, SCREEN_W, TOP_BAR_H, COL_GREEN_DIM);
    draw_text(16, 20, "SCREENSHOT GALLERY", COL_GREEN_BRIGHT);

    if (gallery_count > 0) {
        char count_buf[24];
        snprintf(count_buf, sizeof(count_buf), "%d / %d", gallery_index + 1, gallery_count);
        int cw = measure_text(count_buf);
        draw_text(SCREEN_W - cw - 16, 20, count_buf, COL_DIM);
    }

    /* Image area */
    if (gallery_count == 0) {
        const char* empty_msg = "NO SCREENSHOTS FOUND";
        int ew = measure_text(empty_msg);
        draw_text(SCREEN_W / 2 - ew / 2, CENTER_Y_MID - 8, empty_msg, COL_DIM);
    } else {
        gallery_load_current();

        if (gallery_texture) {
            int tex_w = vita2d_texture_get_width(gallery_texture);
            int tex_h = vita2d_texture_get_height(gallery_texture);

            /* Scale to fit center area */
            float scale_x = (float)SCREEN_W / tex_w;
            float scale_y = (float)CENTER_H / tex_h;
            float scale = (scale_x < scale_y) ? scale_x : scale_y;
            int scaled_w = (int)(tex_w * scale);
            int scaled_h = (int)(tex_h * scale);
            float ox = (SCREEN_W - scaled_w) / 2.0f;
            float oy = TOP_BAR_H + (CENTER_H - scaled_h) / 2.0f;
            vita2d_draw_texture_scale(gallery_texture, ox, oy, scale, scale);
        } else {
            const char* err_msg = "FAILED TO LOAD IMAGE";
            int ew = measure_text(err_msg);
            draw_text(SCREEN_W / 2 - ew / 2, CENTER_Y_MID - 8, err_msg, COL_RED);
        }

        /* Filename label */
        int fw = measure_text(gallery_files[gallery_index]);
        draw_text(SCREEN_W / 2 - fw / 2, SCREEN_H - BOTTOM_BAR_H - 24,
                  gallery_files[gallery_index], COL_DIM);

        /* Navigation arrows */
        if (gallery_index > 0) {
            draw_text(12, CENTER_Y_MID - 8, "<", COL_WHITE);
        }
        if (gallery_index < gallery_count - 1) {
            int aw = measure_text(">");
            draw_text(SCREEN_W - aw - 12, CENTER_Y_MID - 8, ">", COL_WHITE);
        }
    }

    /* Delete confirmation overlay */
    if (gallery_delete_confirm && gallery_count > 0) {
        const char* confirm_msg = "PRESS TRIANGLE AGAIN TO DELETE";
        int cmw = measure_text(confirm_msg);
        int cm_x = SCREEN_W / 2 - cmw / 2;
        int cm_y = SCREEN_H - BOTTOM_BAR_H - 50;
        vita2d_draw_rectangle(cm_x - 10, cm_y - 4, cmw + 20, 24, RGBA8(40, 20, 20, 220));
        draw_text(cm_x, cm_y, confirm_msg, COL_RED);
    }

    /* Bottom bar */
    vita2d_draw_rectangle(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, BOTTOM_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, SCREEN_H - BOTTOM_BAR_H, COL_GREEN_DIM);
    int hint_y = SCREEN_H - BOTTOM_BAR_H + 20;
    int hx = 40;

    if (gallery_count > 1) {
        draw_text(hx, hint_y, "D-PAD", COL_WHITE);
        hx += measure_text("D-PAD") + 8;
        draw_text(hx, hint_y, "BROWSE", COL_DIM);
        hx += measure_text("BROWSE") + 30;
    }

    if (gallery_count > 0) {
        draw_btn_triangle(hx + 8, hint_y + 9, 7, COL_PS_TRIANGLE);
        hx += 22;
        draw_text(hx, hint_y, "DELETE", COL_DIM);
        hx += measure_text("DELETE") + 30;
    }

    draw_text(hx, hint_y, "SEL", COL_WHITE);
    hx += measure_text("SEL") + 8;
    draw_text(hx, hint_y, "CLOSE", COL_DIM);
}

int ui_is_log_viewer_active(void) {
    return log_viewer_active;
}

/* === Help / How-To Screen === */

static int help_page = 0;
#define HELP_PAGES 3

typedef struct {
    const char* label;
    const char* desc;
} HelpEntry;

typedef struct {
    const char* label;
    const char* desc;
    unsigned int color;  /* 0 = use default COL_WHITE */
} HelpControlEntry;

static HelpControlEntry help_controls[] = {
    {"L Stick",       "Drive (linear / angular)", 0},
    {"R Stick",       "Camera pan / tilt",        0},
    {"L / R Trigger", "Lifter up / down",         0},
    {"Circle",        "E-Stop (latching toggle)", 0},  /* color set at runtime */
    {"Cross",         "Screenshot / Play sound",  0},
    {"Square",        "Sound board",              0},
    {"Triangle",      "Next robot",               0},
    {"D-Pad U/D",     "Speed preset cycle",       0},
    {"D-Pad L/R",     "Switch robot",             0},
    {"Start",         "Config screen",            0},
    {"Select",        "Log / Gallery cycle",      0},
    {"Rear Touch",    "Camera pan/tilt (alt)",    0},
};
#define HELP_CONTROLS_COUNT (sizeof(help_controls) / sizeof(help_controls[0]))

static const HelpEntry help_features[] = {
    {"Speed Presets", "SLOW 0.3x / MED 0.6x / FAST 1.0x"},
    {"Screenshots",   "PNG saved to ux0:data/..."},
    {"Gallery",       "Browse saved screenshots"},
    {"Sound Board",   "5 sounds played via robot speaker"},
    {"Log Viewer",    "Last 16 log entries overlay"},
    {"Battery Alert", "Audio chirp at 50% / 30% / 15%"},
    {"Auto-dim",      "HUD dims after 5s idle"},
    {"E-Stop",        "Latching with audio feedback"},
    {"Config",        "Per-robot IP, camera, stream"},
};
#define HELP_FEATURES_COUNT (sizeof(help_features) / sizeof(help_features[0]))

static int help_colors_init = 0;
static void help_init_colors(void) {
    if (help_colors_init) return;
    help_controls[3].color = COL_PS_CIRCLE;    /* Circle */
    help_controls[4].color = COL_PS_CROSS;     /* Cross */
    help_controls[5].color = COL_PS_SQUARE;    /* Square */
    help_controls[6].color = COL_PS_TRIANGLE;  /* Triangle */
    help_colors_init = 1;
}

static void help_handle_input(void) {
    unsigned int pressed = input_get_buttons_pressed();
    if (pressed & SCE_CTRL_RIGHT) {
        if (help_page < HELP_PAGES - 1) help_page++;
    }
    if (pressed & SCE_CTRL_LEFT) {
        if (help_page > 0) help_page--;
    }
}

static void draw_help_screen(void) {
    help_init_colors();

    /* Top bar */
    vita2d_draw_rectangle(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, TOP_BAR_H - 1, SCREEN_W, TOP_BAR_H - 1, COL_GREEN_DIM);
    draw_text(20, (TOP_BAR_H - FONT_SIZE) / 2, "HELP / HOW-TO", COL_GREEN_BRIGHT);

    {
        char ver_buf[32];
        snprintf(ver_buf, sizeof(ver_buf), "v%s", APP_VERSION);
        int vw = measure_text(ver_buf);
        draw_text(SCREEN_W - vw - 20, (TOP_BAR_H - FONT_SIZE) / 2, ver_buf, COL_DIM);
    }

    /* Page indicator */
    {
        char page_buf[16];
        snprintf(page_buf, sizeof(page_buf), "Page %d/%d", help_page + 1, HELP_PAGES);
        int pw = measure_text(page_buf);
        draw_text(SCREEN_W - pw - 20, TOP_BAR_H + 12, page_buf, COL_DIM);
    }

    /* Content area */
    int content_y = TOP_BAR_H + 10;
    int label_x = 40;
    int desc_x = 250;
    int line_h = 28;

    if (help_page == 0) {
        /* Page 1: Controls */
        draw_text(label_x, content_y, "CONTROLS", COL_GREEN);
        content_y += 8;
        vita2d_draw_line(label_x, content_y + 16, SCREEN_W - 40, content_y + 16, COL_GREEN_DIM);
        content_y += 24;

        for (int i = 0; i < (int)HELP_CONTROLS_COUNT; i++) {
            unsigned int lbl_col = help_controls[i].color ? help_controls[i].color : COL_WHITE;
            draw_text(label_x, content_y, help_controls[i].label, lbl_col);
            draw_text(desc_x, content_y, help_controls[i].desc, COL_DIM);
            content_y += line_h;
        }
    } else if (help_page == 1) {
        /* Page 2: Features */
        draw_text(label_x, content_y, "FEATURES", COL_GREEN);
        content_y += 8;
        vita2d_draw_line(label_x, content_y + 16, SCREEN_W - 40, content_y + 16, COL_GREEN_DIM);
        content_y += 24;

        for (int i = 0; i < (int)HELP_FEATURES_COUNT; i++) {
            draw_text(label_x, content_y, help_features[i].label, COL_WHITE);
            draw_text(desc_x, content_y, help_features[i].desc, COL_DIM);
            content_y += line_h;
        }
    } else {
        /* Page 3: About / Credits */
        draw_text(label_x, content_y, "ABOUT", COL_GREEN);
        content_y += 8;
        vita2d_draw_line(label_x, content_y + 16, SCREEN_W - 40, content_y + 16, COL_GREEN_DIM);
        content_y += 30;

        char ver_line[64];
        snprintf(ver_line, sizeof(ver_line), "Robot Console v%s", APP_VERSION);
        draw_text(label_x, content_y, ver_line, COL_WHITE);
        content_y += line_h;

        char build_line[64];
        snprintf(build_line, sizeof(build_line), "Built %s %s", BUILD_DATE, BUILD_TIME);
        draw_text(label_x, content_y, build_line, COL_DIM);
        content_y += line_h + 10;

        draw_text(label_x, content_y, "CREDITS", COL_GREEN);
        content_y += 8;
        vita2d_draw_line(label_x, content_y + 16, SCREEN_W - 40, content_y + 16, COL_GREEN_DIM);
        content_y += 30;

        draw_text(label_x, content_y, "Developer", COL_WHITE);
        draw_text(desc_x, content_y, "Ric", COL_DIM);
        content_y += line_h;

        draw_text(label_x, content_y, "Contributor", COL_WHITE);
        draw_text(desc_x, content_y, "SirTechify", COL_DIM);
        content_y += line_h;

        draw_text(label_x, content_y, "Platform", COL_WHITE);
        draw_text(desc_x, content_y, "PS Vita (VitaSDK)", COL_DIM);
        content_y += line_h;

        draw_text(label_x, content_y, "Libraries", COL_WHITE);
        draw_text(desc_x, content_y, "vita2d, libcurl, libjpeg, libpng", COL_DIM);
        content_y += line_h;

        draw_text(label_x, content_y, "License", COL_WHITE);
        draw_text(desc_x, content_y, "MIT", COL_DIM);
    }

    /* Navigation arrows */
    if (help_page > 0)
        draw_text(12, CENTER_Y_MID - 8, "<", COL_WHITE);
    if (help_page < HELP_PAGES - 1) {
        int aw = measure_text(">");
        draw_text(SCREEN_W - aw - 12, CENTER_Y_MID - 8, ">", COL_WHITE);
    }

    /* Bottom bar */
    vita2d_draw_rectangle(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, BOTTOM_BAR_H, COL_BAR_BG);
    vita2d_draw_line(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, SCREEN_H - BOTTOM_BAR_H, COL_GREEN_DIM);
    int hint_y = SCREEN_H - BOTTOM_BAR_H + 20;
    int hx = 40;

    if (HELP_PAGES > 1) {
        draw_text(hx, hint_y, "D-PAD L/R", COL_WHITE);
        hx += measure_text("D-PAD L/R") + 8;
        draw_text(hx, hint_y, "PAGE", COL_DIM);
        hx += measure_text("PAGE") + 30;
    }

    draw_text(hx, hint_y, "SEL", COL_WHITE);
    hx += measure_text("SEL") + 8;
    draw_text(hx, hint_y, "BACK", COL_DIM);
}

void ui_render(const ControlState* state, const RobotStatus* status, const char* robot_ip, int ui_mode) {
    if (ui_mode == UI_MODE_GALLERY) {
        gallery_handle_input();
        draw_gallery_screen();
        return;
    }
    if (ui_mode != UI_MODE_GALLERY && gallery_loaded_index >= 0) {
        /* Left gallery mode — free texture */
        gallery_cleanup();
        gallery_count = 0;
    }
    if (ui_mode == UI_MODE_SOUNDS) {
        sounds_handle_input();
        draw_sounds_screen();
        return;
    }
    if (ui_mode == UI_MODE_HELP) {
        help_handle_input();
        draw_help_screen();
        return;
    }
    if (ui_mode == UI_MODE_CONFIG) {
        config_update_ime();
        config_handle_input();
        draw_config_screen();
        return;
    }

    /* Control screen (default) */
    check_touch_taps();

    /* Camera background (with frame-skip: only convert pixels when a new frame arrives) */
    static int last_rendered_frame = -1;
    int frame_width = 0, frame_height = 0;
    const uint8_t* frame_data = mjpeg_get_frame_data(&frame_width, &frame_height);
    int cur_frame_counter = mjpeg_get_frame_counter();

    if (frame_data && frame_width > 0 && frame_height > 0) {
        if (!camera_texture || camera_tex_width != frame_width || camera_tex_height != frame_height) {
            if (camera_texture) vita2d_free_texture(camera_texture);
            camera_texture = vita2d_create_empty_texture_format(
                frame_width, frame_height, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
            camera_tex_width = frame_width;
            camera_tex_height = frame_height;
            last_rendered_frame = -1;  /* force re-render on resize */
        }
        if (camera_texture) {
            /* Only do expensive RGB24->RGBA32 copy when frame counter changed */
            if (cur_frame_counter != last_rendered_frame) {
                uint32_t* tex_data = (uint32_t*)vita2d_texture_get_datap(camera_texture);
                unsigned int tex_stride = vita2d_texture_get_stride(camera_texture) / 4;
                for (int y = 0; y < frame_height; y++) {
                    const uint8_t* src_row = frame_data + y * frame_width * 3;
                    uint32_t* dst_row = tex_data + y * tex_stride;
                    for (int x = 0; x < frame_width; x++) {
                        dst_row[x] = RGBA8(src_row[x*3], src_row[x*3+1], src_row[x*3+2], 255);
                    }
                }
                last_rendered_frame = cur_frame_counter;
            }
            /* Scale to fit camera area */
            float scale_x = (float)SCREEN_W / frame_width;
            float scale_y = (float)CENTER_H / frame_height;
            float scale = scale_x < scale_y ? scale_x : scale_y;
            int scaled_w = (int)(frame_width * scale);
            int scaled_h = (int)(frame_height * scale);
            float offset_x = (SCREEN_W - scaled_w) / 2.0f;
            float offset_y = TOP_BAR_H + (CENTER_H - scaled_h) / 2.0f;
            vita2d_draw_texture_scale(camera_texture, offset_x, offset_y, scale, scale);
        }
        /* Camera latency indicator */
        {
            static int prev_frame_counter = -1;
            static uint32_t frame_recv_time = 0;
            if (cur_frame_counter != prev_frame_counter) {
                prev_frame_counter = cur_frame_counter;
                frame_recv_time = get_ticks_ms();
            }
            if (frame_recv_time > 0) {
                uint32_t age = get_ticks_ms() - frame_recv_time;
                char lat_buf[16];
                snprintf(lat_buf, sizeof(lat_buf), "%dms", age);
                unsigned int lat_col = (age < 200) ? COL_GREEN : (age < 500) ? COL_AMBER : COL_RED;
                draw_text(SCREEN_W - measure_text(lat_buf) - 12, TOP_BAR_H + 4, lat_buf, lat_col);
            }
        }
    } else {
        draw_no_signal(0, TOP_BAR_H, SCREEN_W, CENTER_H);
    }

    /* Auto-dim: track input activity */
    if (state->linear != 0.0f || state->angular != 0.0f ||
        state->cam_pan != 0.0f || state->cam_tilt != 0.0f ||
        state->lifter != 0 || state->estop) {
        last_input_time = get_ticks_ms();
    }
    int hud_idle = (last_input_time > 0 &&
                    (get_ticks_ms() - last_input_time) > HUD_DIM_DELAY_MS);

    /* HUD overlay elements (dimmed when idle) */
    draw_reticle(state->cam_pan, state->cam_tilt);
    draw_lin_slider(state->linear);
    draw_ang_slider(state->angular);
    draw_lift_buttons(state->lifter);

    /* Dim overlay on HUD when idle (drawn over HUD elements) */
    if (hud_idle) {
        /* Semi-transparent dark wash dims the reticle/sliders/lift area */
        vita2d_draw_rectangle(0, TOP_BAR_H, SCREEN_W, CENTER_H,
                              RGBA8(15, 15, 20, HUD_DIM_ALPHA));
    }

    /* E-stop flash */
    draw_estop_flash(state->estop || (status && status->estop_active));

    /* Speed preset badge (always visible, color-coded) */
    {
        unsigned int badge_col;
        if (speed_preset == SPEED_SLOW) badge_col = COL_GREEN;
        else if (speed_preset == SPEED_MEDIUM) badge_col = COL_AMBER;
        else badge_col = COL_RED;
        const char* label = speed_labels[speed_preset];
        int bw = measure_text(label) + 12;
        draw_rounded_rect_filled(10, TOP_BAR_H + 8, bw, 20, 4, badge_col);
        draw_text(16, TOP_BAR_H + 10, label, COL_DARK_TEXT);
    }

    /* Reconnection overlay */
    if (status && status->connection_state == NETWORK_STATUS_FAIL) {
        static int fail_count = 0;
        fail_count++;
        vita2d_draw_rectangle(SCREEN_W/2 - 140, CENTER_Y_MID - 30, 280, 60, RGBA8(10, 10, 15, 180));
        draw_outline_rect(SCREEN_W/2 - 140, CENTER_Y_MID - 30, 280, 60, COL_RED);

        if ((get_ticks_ms() / 400) % 2) {
            const char* rtxt = "RECONNECTING...";
            int rw = measure_text(rtxt);
            draw_text(SCREEN_W/2 - rw/2, CENTER_Y_MID - 20, rtxt, COL_RED);
        }

        char fc_buf[32];
        snprintf(fc_buf, sizeof(fc_buf), "ATTEMPTS: %d", fail_count / 30);
        int fw = measure_text(fc_buf);
        draw_text(SCREEN_W/2 - fw/2, CENTER_Y_MID + 5, fc_buf, COL_DIM);
    }

    /* Low battery warning flash (<=15%) */
    if (status && status->battery >= 0 && status->battery <= 15) {
        uint32_t now = get_ticks_ms();
        if (batt_warn_start == 0) batt_warn_start = now;
        if (((now - batt_warn_start) / 500) % 2) {
            vita2d_draw_rectangle(0, TOP_BAR_H, SCREEN_W, CENTER_H, RGBA8(180, 0, 0, 60));
        }
    } else {
        batt_warn_start = 0;
    }

    /* Screenshot flash + toast */
    if (screenshot_flash_start) {
        uint32_t elapsed = get_ticks_ms() - screenshot_flash_start;
        if (elapsed < 100) {
            /* Brief white flash */
            vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(255, 255, 255, 120));
        } else if (elapsed < 1500) {
            /* "SAVED" toast */
            const char* stxt = "SCREENSHOT SAVED";
            int sw = measure_text(stxt);
            int tx = SCREEN_W / 2 - sw / 2 - 8;
            int ty = CENTER_Y_MID + 60;
            vita2d_draw_rectangle(tx, ty, sw + 16, 24, RGBA8(0, 0, 0, 160));
            draw_text(tx + 8, ty + 3, stxt, COL_GREEN);
        } else {
            screenshot_flash_start = 0;
        }
    }

    /* Config saved toast (shown after exiting config) */
    if (config_save_flash_time > 0) {
        uint32_t elapsed = get_ticks_ms() - config_save_flash_time;
        if (elapsed < 1500) {
            const char* stxt = "CONFIG SAVED";
            int sw = measure_text(stxt);
            int tx = SCREEN_W / 2 - sw / 2 - 8;
            int ty = CENTER_Y_MID + 90;
            vita2d_draw_rectangle(tx, ty, sw + 16, 24, RGBA8(0, 0, 0, 160));
            draw_text(tx + 8, ty + 3, stxt, COL_GREEN);
        } else {
            config_save_flash_time = 0;
        }
    }

    /* Log viewer overlay */
    if (log_viewer_active) {
        vita2d_draw_rectangle(10, TOP_BAR_H + 5, SCREEN_W - 20, CENTER_H - 10,
                              RGBA8(10, 10, 15, 220));
        draw_outline_rect(10, TOP_BAR_H + 5, SCREEN_W - 20, CENTER_H - 10, COL_GREEN_DIM);
        draw_text(20, TOP_BAR_H + 10, "LOG (SELECT to close)", COL_GREEN);

        char log_lines[16][128];
        int count = log_get_recent(log_lines, 16);
        for (int i = 0; i < count; i++) {
            draw_text(20, TOP_BAR_H + 30 + i * 18, log_lines[i], COL_DIM);
        }
    }

    /* Bars drawn last */
    draw_top_bar(status, robot_ip);
    draw_bottom_bar(state);

    /* FPS counter */
    fps_frame_count++;
    uint32_t now = get_ticks_ms();
    if (now - fps_last_time >= 1000) {
        fps_display = fps_frame_count;
        fps_frame_count = 0;
        fps_last_time = now;
    }
    char fps_buf[8];
    snprintf(fps_buf, sizeof(fps_buf), "%dFPS", fps_display);
    draw_text(SCREEN_W - measure_text(fps_buf) - 10, SCREEN_H - 18, fps_buf, COL_DIM);
}

/* Called from main loop between end_drawing and swap_buffers */
void ui_check_screenshot(void) {
    if (screenshot_pending) {
        do_save_screenshot();
        screenshot_pending = 0;
    }
}
