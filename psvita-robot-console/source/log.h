#ifndef LOG_H
#define LOG_H

#define LOG_PATH "ux0:data/robot_console.log"

void log_init(void);
void log_write(const char* fmt, ...);
void log_close(void);

/* Get recent log lines for on-screen display.
 * Returns number of lines filled. Each line is max line_len chars. */
int log_get_recent(char lines[][128], int max_lines);

#endif
