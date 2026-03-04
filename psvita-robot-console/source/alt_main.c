#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include "version.h"
#include "input.h"
#include "network.h"
#include "robot.h"
#include "camera.h"
#include "mjpeg.h"
#include "ui.h"

#define NET_MEM_SIZE (1024 * 1024)

static unsigned char net_memory[NET_MEM_SIZE];

int main(void) {

    printf("[MAIN] Robot Console v%s (built %s %s)\n",
           APP_VERSION, BUILD_DATE, BUILD_TIME);

    /* -----------------------------
       Load Vita system modules
    ------------------------------ */
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);

    printf("[MAIN] System modules loaded\n");

    /* -----------------------------
       Initialize Vita Networking
    ------------------------------ */
    SceNetInitParam netInitParam;
    netInitParam.memory = net_memory;
    netInitParam.size   = NET_MEM_SIZE;
    netInitParam.flags  = 0;

    int ret = sceNetInit(&netInitParam);
    printf("[NET] sceNetInit: 0x%X\n", ret);

    ret = sceNetCtlInit();
    printf("[NET] sceNetCtlInit: 0x%X\n", ret);

    if (ret != 0) {
        printf("[NET] WARNING: NetCtl init failed\n");
    }

    /* -----------------------------
       Initialize SDL
    ------------------------------ */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        printf("[ERROR] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Robot Console",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        960,
        544,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        printf("[ERROR] SDL_CreateWindow failed\n");
        return -1;
    }

    /* Avoid accelerated renderer issues on Vita */
    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, 0);

    if (!renderer) {
        printf("[ERROR] SDL_CreateRenderer failed\n");
        return -1;
    }

    SDL_RenderSetLogicalSize(renderer, 960, 544);
    SDL_RenderSetViewport(renderer, NULL);

    printf("[MAIN] SDL initialized\n");

    /* -----------------------------
       Initialize Subsystems
    ------------------------------ */
    ui_init(renderer);
    input_init();
    robot_init();
    camera_config_load();

    /* -----------------------------
       Build MJPEG stream URL
    ------------------------------ */
    {
        const CameraConfig* cam_cfg = camera_get_config();
        int idx = robot_get_active_index();

        const char* cam_host = robot_get_camera_host(idx);
        const char* host =
            (cam_host[0] != '\0')
                ? cam_host
                : robot_get_active_ip();

        int port = robot_get_camera_port(idx);
        const char* endpoint =
            robot_get_stream_endpoint(idx);

        char stream_url[256];

        if (endpoint[0] != '/' &&
            endpoint[0] != '\0') {
            snprintf(stream_url,
                     sizeof(stream_url),
                     "http://%s:%d/%s",
                     host, port, endpoint);
        } else {
            snprintf(stream_url,
                     sizeof(stream_url),
                     "http://%s:%d%s",
                     host, port, endpoint);
        }

        printf("[MAIN] Stream URL: %s\n", stream_url);

        mjpeg_init(stream_url, cam_cfg->boundary);
        mjpeg_start();
    }

    network_init(robot_get_active_ip());

    printf("[MAIN] Initialization complete\n");

    /* -----------------------------
       Main Loop
    ------------------------------ */
    ControlState state = {0};
    RobotStatus status = {0};
    status.connection_state = NETWORK_STATUS_FAIL;

    int ui_mode = UI_MODE_CONTROL;
    int prev_triangle = 0;
    int running = 1;
    int frame = 0;

    while (running) {

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                running = 0;
        }

        input_update(&state);

        /* Toggle UI mode */
        if (state.switch_robot && !prev_triangle) {

            if (ui_mode == UI_MODE_CONFIG) {

                robot_save_config();
                network_init(robot_get_active_ip());

                mjpeg_cleanup();

                const CameraConfig* cam_cfg =
                    camera_get_config();

                int idx =
                    robot_get_active_index();

                const char* cam_host =
                    robot_get_camera_host(idx);

                const char* host =
                    (cam_host[0] != '\0')
                        ? cam_host
                        : robot_get_active_ip();

                int port =
                    robot_get_camera_port(idx);

                const char* endpoint =
                    robot_get_stream_endpoint(idx);

                char stream_url[256];

                if (endpoint[0] != '/' &&
                    endpoint[0] != '\0') {
                    snprintf(stream_url,
                             sizeof(stream_url),
                             "http://%s:%d/%s",
                             host, port, endpoint);
                } else {
                    snprintf(stream_url,
                             sizeof(stream_url),
                             "http://%s:%d%s",
                             host, port, endpoint);
                }

                printf("[MAIN] Restarting stream: %s\n",
                       stream_url);

                mjpeg_init(stream_url,
                           cam_cfg->boundary);
                mjpeg_start();

                ui_mode = UI_MODE_CONTROL;

            } else {
                ui_mode = UI_MODE_CONFIG;
            }

            printf("[MAIN] Mode: %s\n",
                   ui_mode == UI_MODE_CONTROL
                       ? "CONTROL"
                       : "CONFIG");
        }

        prev_triangle = state.switch_robot;

        /* Network at 30Hz */
        if ((frame % 2) == 0) {

            float linear = state.linear;
            float angular = state.angular;

            if (state.estop) {
                linear = 0.0f;
                angular = 0.0f;
            }

            network_send_control(
                linear,
                angular,
                state.cam_pan,
                state.cam_tilt,
                state.lifter,
                state.estop
            );

            network_fetch_status(&status);
        }

        frame++;

        ui_render(&state,
                  &status,
                  robot_get_active_ip(),
                  ui_mode);

        SDL_RenderPresent(renderer);

        SDL_Delay(16);  // ~60 FPS
    }

    /* -----------------------------
       Cleanup
    ------------------------------ */
    mjpeg_cleanup();
    ui_cleanup();
    SDL_Quit();

    sceNetCtlTerm();
    sceNetTerm();

    printf("[MAIN] Shutdown complete\n");

    return 0;
}