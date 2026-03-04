#include <stdio.h>
#include <vita2d.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include "version.h"
#include "input.h"
#include "network.h"
#include "robot.h"
#include "camera.h"
#include "mjpeg.h"
#include "ui.h"
#include "feedback.h"
#include "log.h"

int main(void) {
    printf("[MAIN] Robot Console v%s (built %s %s)\n", APP_VERSION, BUILD_DATE, BUILD_TIME);

    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);

    printf("[MAIN] System modules loaded\n");

    vita2d_init();
    vita2d_set_clear_color(RGBA8(15, 15, 20, 255));

    log_init();
    log_write("[MAIN] Robot Console v%s (built %s %s)", APP_VERSION, BUILD_DATE, BUILD_TIME);
    log_write("[MAIN] System modules loaded, vita2d initialized");

    ui_init();

    /* Splash screen during initialization */
    ui_draw_splash("INITIALIZING...");
    sceKernelDelayThread(200 * 1000);

    int ui_mode = UI_MODE_CONTROL;

    input_init();
    ui_draw_splash("LOADING CONFIG...");
    robot_init();
    camera_config_load();
    feedback_init();
    feedback_set_ui_mute(robot_get_ui_sounds_muted());
    log_write("[MAIN] UI/input/robot/camera/audio initialized");

    /* Initialize network stack FIRST — curl depends on it on real hardware */
    ui_draw_splash("CONNECTING...");
    network_init(robot_get_active_ip());
    log_write("[MAIN] Network initialized, robot IP: %s", robot_get_active_ip());

    /* Connection test with timeout — let user skip to config if robot unreachable */
    {
        int connected = 0;
        int skip_to_config = 0;
        for (int attempt = 0; attempt < 3 && !connected && !skip_to_config; attempt++) {
            char splash_msg[64];
            snprintf(splash_msg, sizeof(splash_msg), "CONNECTING... (%d/3)", attempt + 1);
            ui_draw_splash(splash_msg);

            connected = network_test_connection(robot_get_active_ip());
            if (connected) break;

            /* Check if user pressed Start to skip */
            ControlState skip_state = {0};
            input_update(&skip_state);
            if (skip_state.open_config) {
                skip_to_config = 1;
                log_write("[MAIN] User skipped connection — going to config");
            }
        }

        if (!connected && !skip_to_config) {
            ui_draw_splash("NO RESPONSE - START FOR CONFIG");
            log_write("[MAIN] Robot unreachable after 3 attempts");
            /* Wait for user to press Start or just continue after 3s */
            int waited = 0;
            while (waited < 3000) {
                ControlState skip_state = {0};
                input_update(&skip_state);
                if (skip_state.open_config) {
                    skip_to_config = 1;
                    break;
                }
                sceKernelDelayThread(50 * 1000);
                waited += 50;
            }
        }

        /* Build stream URL from per-robot config */
        const CameraConfig* cam_cfg = camera_get_config();
        int idx = robot_get_active_index();
        const char* cam_host = robot_get_camera_host(idx);
        const char* host = (cam_host[0] != '\0') ? cam_host : robot_get_active_ip();
        int port = robot_get_camera_port(idx);
        const char* endpoint = robot_get_stream_endpoint(idx);
        char stream_url[256];

        if (endpoint[0] != '/' && endpoint[0] != '\0') {
            snprintf(stream_url, sizeof(stream_url), "http://%s:%d/%s", host, port, endpoint);
        } else {
            snprintf(stream_url, sizeof(stream_url), "http://%s:%d%s", host, port, endpoint);
        }
        log_write("[MAIN] Stream URL: %s", stream_url);
        log_write("[MAIN] Boundary: %s", cam_cfg->boundary);

        int rc = mjpeg_init(stream_url, cam_cfg->boundary);
        log_write("[MAIN] mjpeg_init returned: %d", rc);
        rc = mjpeg_start();
        log_write("[MAIN] mjpeg_start returned: %d", rc);

        if (skip_to_config) {
            ui_mode = UI_MODE_CONFIG;
            log_write("[MAIN] Starting in CONFIG mode (connection failed)");
        }
    }

    /* Load initial speed preset from robot config */
    ui_set_speed_preset(robot_get_speed_preset(robot_get_active_index()));

    if (ui_mode != UI_MODE_CONFIG) {
        ui_draw_splash("READY");
        feedback_tone_ready();
        sceKernelDelayThread(500 * 1000);
    }

    /* Flush both framebuffers to clear any splash screen remnants */
    for (int i = 0; i < 2; i++) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    log_write("[MAIN] Initialization complete, entering main loop");

    ControlState state = {0};
    RobotStatus status = {0};
    status.connection_state = NETWORK_STATUS_FAIL;

    int running = 1;
    int frame = 0;
    int prev_estop = 0;
    int prev_connection = NETWORK_STATUS_FAIL;
    int batt_alerted_50 = 0;
    int batt_alerted_30 = 0;
    int batt_alerted_15 = 0;

    while (running) {
        input_update(&state);
        
        /* Start button: toggle config mode (or return from help) */
        if (state.open_config && ui_mode == UI_MODE_HELP) {
            ui_mode = UI_MODE_CONFIG;
            log_write("[MAIN] Returned to CONFIG from HELP");
        } else if (state.open_config) {
            if (ui_mode == UI_MODE_CONFIG) {
                /* Leaving config -- save and re-init network + stream */
                robot_set_speed_preset(robot_get_active_index(), ui_get_speed_preset());
                robot_save_config();
                network_init(robot_get_active_ip());

                /* Restart MJPEG stream with possibly changed config */
                mjpeg_cleanup();
                {
                    const CameraConfig* cam_cfg = camera_get_config();
                    int idx = robot_get_active_index();
                    const char* cam_host = robot_get_camera_host(idx);
                    const char* host = (cam_host[0] != '\0') ? cam_host : robot_get_active_ip();
                    int port = robot_get_camera_port(idx);
                    const char* endpoint = robot_get_stream_endpoint(idx);
                    char stream_url[256];

                    if (endpoint[0] != '/' && endpoint[0] != '\0') {
                        snprintf(stream_url, sizeof(stream_url), "http://%s:%d/%s", host, port, endpoint);
                    } else {
                        snprintf(stream_url, sizeof(stream_url), "http://%s:%d%s", host, port, endpoint);
                    }
                    log_write("[MAIN] Restarting stream: %s", stream_url);
                    mjpeg_init(stream_url, cam_cfg->boundary);
                    mjpeg_start();
                }
                ui_notify_config_saved();
                feedback_tone_save();
                ui_mode = UI_MODE_CONTROL;
            } else {
                ui_mode = UI_MODE_CONFIG;
            }
            log_write("[MAIN] Switched to %s mode", ui_mode == UI_MODE_CONTROL ? "CONTROL" : "CONFIG");
        }

        /* Square button: toggle sounds mode (not from config) */
        if (state.sound_screen && ui_mode != UI_MODE_CONFIG) {
            ui_mode = (ui_mode == UI_MODE_SOUNDS) ? UI_MODE_CONTROL : UI_MODE_SOUNDS;
            log_write("[MAIN] Switched to %s mode", ui_mode == UI_MODE_SOUNDS ? "SOUNDS" : "CONTROL");
        }

        /* Speed preset cycling (D-Pad Up/Down) */
        if (state.speed_change != 0) {
            int p = ui_get_speed_preset() + state.speed_change;
            if (p < 0) p = 0;
            if (p >= SPEED_COUNT) p = SPEED_COUNT - 1;
            if (p != ui_get_speed_preset()) {
                ui_set_speed_preset(p);
                feedback_tone_speed(p);
            }
        }

        /* Screenshot on Cross button */
        if (state.screenshot) {
            ui_save_screenshot();
            log_write("[MAIN] Screenshot saved");
        }

        /* E-Stop audio feedback on toggle */
        if (state.estop != prev_estop) {
            if (state.estop)
                feedback_tone_estop_on();
            else
                feedback_tone_estop_off();
            prev_estop = state.estop;
        }

        /* Connection loss audio alert */
        if (status.connection_state == NETWORK_STATUS_FAIL &&
            prev_connection != NETWORK_STATUS_FAIL) {
            feedback_tone_alert();
        }
        prev_connection = status.connection_state;

        /* Battery threshold audio alerts (50%, 30%, 15%) */
        if (status.connection_state == NETWORK_STATUS_OK ||
            status.connection_state == NETWORK_STATUS_SLOW) {
            if (status.battery <= 15 && !batt_alerted_15) {
                feedback_tone_battery();
                log_write("[MAIN] Battery alert: %d%% (critical)", status.battery);
                batt_alerted_15 = 1;
            } else if (status.battery <= 30 && !batt_alerted_30) {
                feedback_tone_battery();
                log_write("[MAIN] Battery alert: %d%% (low)", status.battery);
                batt_alerted_30 = 1;
            } else if (status.battery <= 50 && !batt_alerted_50) {
                feedback_tone_battery();
                log_write("[MAIN] Battery alert: %d%% (half)", status.battery);
                batt_alerted_50 = 1;
            }
            /* Reset alerts if battery recovers (e.g. robot swap) */
            if (status.battery > 50) {
                batt_alerted_50 = 0;
                batt_alerted_30 = 0;
                batt_alerted_15 = 0;
            } else if (status.battery > 30) {
                batt_alerted_30 = 0;
                batt_alerted_15 = 0;
            } else if (status.battery > 15) {
                batt_alerted_15 = 0;
            }
        }

        /* Select button: open help from config, or return from help */
        if (state.select_pressed && ui_mode == UI_MODE_HELP) {
            ui_mode = UI_MODE_CONFIG;
            log_write("[MAIN] Returned to CONFIG from HELP");
        } else if (state.select_pressed && ui_mode == UI_MODE_CONFIG) {
            ui_mode = UI_MODE_HELP;
            log_write("[MAIN] Opened HELP from CONFIG");
        }

        /* Select button: cycle overlays (off -> log -> gallery -> off) */
        else if (state.select_pressed) {
            if (ui_mode == UI_MODE_GALLERY) {
                /* Gallery -> off (back to control) */
                ui_mode = UI_MODE_CONTROL;
                log_write("[MAIN] Gallery closed");
            } else if (ui_mode == UI_MODE_CONTROL && ui_is_log_viewer_active()) {
                /* Log viewer on -> gallery */
                ui_toggle_log_viewer();  /* turn off log viewer */
                ui_mode = UI_MODE_GALLERY;
                log_write("[MAIN] Switched to GALLERY mode");
            } else if (ui_mode == UI_MODE_CONTROL) {
                /* Off -> log viewer on */
                ui_toggle_log_viewer();
            }
        }

        /* Triangle/D-pad LR/touch robot name: cycle active robot (in control mode) */
        int robot_dir = 0;
        if (state.switch_robot || ui_robot_name_tapped()) robot_dir = 1;
        else if (state.switch_robot_lr) robot_dir = state.switch_robot_lr;

        if (robot_dir != 0 && ui_mode == UI_MODE_CONTROL) {
            /* Save speed preset to current robot before switching */
            robot_set_speed_preset(robot_get_active_index(), ui_get_speed_preset());

            robot_switch(robot_dir);
            feedback_tone_switch();
            network_init(robot_get_active_ip());

            /* Load speed preset from new robot */
            ui_set_speed_preset(robot_get_speed_preset(robot_get_active_index()));

            /* Restart MJPEG stream for new robot */
            mjpeg_cleanup();
            {
                const CameraConfig* cam_cfg = camera_get_config();
                int idx = robot_get_active_index();
                const char* cam_host = robot_get_camera_host(idx);
                const char* host = (cam_host[0] != '\0') ? cam_host : robot_get_active_ip();
                int port = robot_get_camera_port(idx);
                const char* endpoint = robot_get_stream_endpoint(idx);
                char stream_url[256];

                if (endpoint[0] != '/' && endpoint[0] != '\0') {
                    snprintf(stream_url, sizeof(stream_url), "http://%s:%d/%s", host, port, endpoint);
                } else {
                    snprintf(stream_url, sizeof(stream_url), "http://%s:%d%s", host, port, endpoint);
                }
                log_write("[MAIN] Robot switched to %s, stream: %s", robot_get_active_ip(), stream_url);
                mjpeg_init(stream_url, cam_cfg->boundary);
                mjpeg_start();
            }
        }

        /* Network at ~30 Hz so main loop stays at 60 FPS (design: control 30Hz, UI non-blocking) */
        if ((frame % 2) == 0) {
            float speed = ui_get_speed_scale();
            float linear = state.linear * speed;
            float angular = state.angular * speed;
            if (state.estop) {
                linear = 0.0f;
                angular = 0.0f;
            }
            network_send_control(linear, angular,
                state.cam_pan, state.cam_tilt,
                state.lifter, state.estop);
            network_fetch_status(&status);
        }
        frame++;

        vita2d_start_drawing();
        vita2d_clear_screen();

        ui_render(&state, &status, robot_get_active_ip(), ui_mode);

        vita2d_common_dialog_update();
        vita2d_end_drawing();
        ui_check_screenshot();
        vita2d_swap_buffers();

        sceKernelDelayThread(16 * 1000);  /* ~60 FPS */
    }

    feedback_cleanup();
    mjpeg_cleanup();
    ui_cleanup();
    log_write("[MAIN] Shutting down");
    log_close();
    vita2d_fini();
    return 0;
}
