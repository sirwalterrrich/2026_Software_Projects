#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>
#include "network.h"
#include "log.h"

#define FLASK_PORT "5000"
#define CONTROL_TIMEOUT_MS  80   /* Control POST: tight for responsiveness */
#define STATUS_TIMEOUT_MS  150   /* Status GET: slightly longer to tolerate jitter */
#define SOUND_TIMEOUT_MS   800   /* Sound POST: can afford to wait */
#define TEST_TIMEOUT_MS   2500   /* Connection test: one-shot, generous */

static char base_url[64];
static int net_initialized;
static uint32_t last_ok_time = 0;

static void ensure_net(void) {
    if (net_initialized) return;
    SceNetInitParam p = { .memory = malloc(1024 * 1024), .size = 1024 * 1024, .flags = 0 };
    if (!p.memory) {
        log_write("[NET] FAILED to allocate 1MB for sceNetInit!");
        return;
    }
    int rc = sceNetInit(&p);
    log_write("[NET] sceNetInit returned: 0x%08X", rc);
    rc = sceNetCtlInit();
    log_write("[NET] sceNetCtlInit returned: 0x%08X", rc);
    CURLcode crc = curl_global_init(CURL_GLOBAL_ALL);
    log_write("[NET] curl_global_init returned: %d", crc);
    net_initialized = 1;
    log_write("[NET] Network stack initialized OK");
}
static char status_response[512];
static size_t status_len;

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    if (total >= sizeof(status_response)) total = sizeof(status_response) - 1;
    memcpy(status_response, ptr, total);
    status_response[total] = '\0';
    status_len = total;
    return size * nmemb;
}

void network_init(const char* robot_ip) {
    ensure_net();
    snprintf(base_url, sizeof(base_url), "http://%s:%s", robot_ip, FLASK_PORT);
}

static void post_control(float linear, float angular,
                        float cam_pan, float cam_tilt,
                        int lifter, int estop) {
    char url[80];
    char body[160];
    snprintf(url, sizeof(url), "%s/control", base_url);
    snprintf(body, sizeof(body),
             "{\"linear\":%.2f,\"angular\":%.2f,\"cam_pan\":%.2f,\"cam_tilt\":%.2f,\"lifter\":%d,\"estop\":%s}",
             linear, angular, cam_pan, cam_tilt, lifter, estop ? "true" : "false");

    CURL* curl = curl_easy_init();
    if (!curl) return;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)CONTROL_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)CONTROL_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void network_send_control(float linear, float angular,
                          float cam_pan, float cam_tilt,
                          int lifter, int estop) {
    post_control(linear, angular, cam_pan, cam_tilt, lifter, estop);
}

int network_fetch_status(RobotStatus* out) {
    char url[80];
    snprintf(url, sizeof(url), "%s/status", base_url);
    status_response[0] = '\0';
    status_len = 0;

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (out) out->connection_state = NETWORK_STATUS_FAIL;
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)STATUS_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)STATUS_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (out) {
        out->battery = 0;
        out->ping_ms = -1;
        out->lifter_position = 0;
        out->estop_active = 0;
    }

    if (res != CURLE_OK || http_code != 200) {
        if (out) out->connection_state = NETWORK_STATUS_FAIL;
        return 0;
    }

    if (!out) return 1;

    out->connection_state = NETWORK_STATUS_OK;
    last_ok_time = (uint32_t)(sceKernelGetProcessTimeWide() / 1000);

    /* Parse JSON fields individually (tolerates spaces from Python json) */
    const char* p;
    if ((p = strstr(status_response, "\"battery\"")) != NULL)
        sscanf(p, "\"battery\"%*[: ]%d", &out->battery);
    if ((p = strstr(status_response, "\"ping_ms\"")) != NULL)
        sscanf(p, "\"ping_ms\"%*[: ]%d", &out->ping_ms);
    if ((p = strstr(status_response, "\"lifter_position\"")) != NULL)
        sscanf(p, "\"lifter_position\"%*[: ]%d", &out->lifter_position);
    if (strstr(status_response, "\"estop_active\": true") != NULL ||
        strstr(status_response, "\"estop_active\":true") != NULL)
        out->estop_active = 1;

    if (out->ping_ms > 150) out->connection_state = NETWORK_STATUS_SLOW;
    return 1;
}

uint32_t network_get_last_ok_age_ms(void) {
    if (last_ok_time == 0) return 0;
    uint32_t now = (uint32_t)(sceKernelGetProcessTimeWide() / 1000);
    return now - last_ok_time;
}

int network_play_sound(int sound_id) {
    char url[80];
    char body[64];
    snprintf(url, sizeof(url), "%s/sound", base_url);
    snprintf(body, sizeof(body), "{\"sound_id\":%d}", sound_id);

    CURL* curl = curl_easy_init();
    if (!curl) return 0;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)SOUND_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)SOUND_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK && http_code == 200) ? 1 : 0;
}

int network_get_rssi(void) {
    SceNetCtlInfo info;
    memset(&info, 0, sizeof(info));
    /* SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE = 6 */
    int ret = sceNetCtlInetGetInfo(6, &info);
    if (ret < 0) return 0;
    return (int)info.rssi_percentage;
}

int network_test_connection(const char* ip) {
    ensure_net();
    char url[80];
    snprintf(url, sizeof(url), "http://%s:%s/status", ip, FLASK_PORT);

    CURL* curl = curl_easy_init();
    if (!curl) return 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)TEST_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)TEST_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK && http_code == 200) ? 1 : 0;
}
