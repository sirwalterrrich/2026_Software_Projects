#include "feedback.h"
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

/*
 * Audio feedback via a dedicated thread.
 * sceAudioOutOutput() is blocking, so we run tone generation
 * on a separate thread to avoid stalling the main loop.
 *
 * Note: PS Vita handheld has no vibration motor.
 * sceCtrlSetActuator only works on paired DS3/DS4 controllers.
 */

#define SAMPLE_RATE  48000
#define BUF_SAMPLES  512   /* must be multiple of 64 */
#define PI2 (2.0f * 3.14159265f)

/* Tone request (written by main thread, read by audio thread) */
typedef struct {
    float freq_start;
    float freq_end;
    int duration_samples;
    int sweep;      /* 1 = sweep from start to end freq */
} ToneRequest;

static volatile ToneRequest pending_tone = {0};
static volatile int tone_requested = 0;
static volatile int audio_running = 0;
static volatile int ui_muted = 0;
static SceUID audio_thread_id = -1;
static int audio_port = -1;

static int audio_thread_func(SceSize args, void *argp) {
    (void)args; (void)argp;

    audio_port = sceAudioOutOpenPort(
        SCE_AUDIO_OUT_PORT_TYPE_MAIN,
        BUF_SAMPLES,
        SAMPLE_RATE,
        SCE_AUDIO_OUT_MODE_MONO
    );

    if (audio_port < 0) {
        printf("[FEEDBACK] Failed to open audio port: 0x%08X\n", audio_port);
        return 0;
    }

    printf("[FEEDBACK] Audio thread started, port=%d\n", audio_port);

    int16_t buf[BUF_SAMPLES];

    while (audio_running) {
        if (!tone_requested) {
            /* Sleep 10ms waiting for a tone request */
            sceKernelDelayThread(10000);
            continue;
        }

        /* Copy request atomically enough for our purposes */
        ToneRequest req;
        req.freq_start = pending_tone.freq_start;
        req.freq_end = pending_tone.freq_end;
        req.duration_samples = pending_tone.duration_samples;
        req.sweep = pending_tone.sweep;
        tone_requested = 0;

        /* Generate and play the tone */
        float phase = 0.0f;
        int remaining = req.duration_samples;
        int total = req.duration_samples;

        while (remaining > 0 && audio_running) {
            int count = BUF_SAMPLES;
            if (count > remaining) count = remaining;

            int played = total - remaining;
            for (int i = 0; i < count; i++) {
                float t = (float)(played + i) / (float)total;
                float freq = req.sweep
                    ? (req.freq_start + (req.freq_end - req.freq_start) * t)
                    : req.freq_start;
                float val = sinf(phase) * 16000.0f;

                /* Fade out last 20% to avoid click */
                if (t > 0.8f)
                    val *= (1.0f - t) / 0.2f;

                buf[i] = (int16_t)val;
                phase += PI2 * freq / SAMPLE_RATE;
                if (phase > PI2) phase -= PI2;
            }

            /* Zero-fill remainder of buffer */
            for (int i = count; i < BUF_SAMPLES; i++)
                buf[i] = 0;

            sceAudioOutOutput(audio_port, buf);  /* blocks until hw consumes */
            remaining -= count;

            /* If a new tone was requested, abort current one */
            if (tone_requested) break;
        }
    }

    sceAudioOutReleasePort(audio_port);
    audio_port = -1;
    printf("[FEEDBACK] Audio thread exiting\n");
    return sceKernelExitDeleteThread(0);
}

static void request_tone(float freq_start, float freq_end, int duration_ms, int sweep) {
    pending_tone.freq_start = freq_start;
    pending_tone.freq_end = freq_end;
    pending_tone.duration_samples = (SAMPLE_RATE * duration_ms) / 1000;
    pending_tone.sweep = sweep;
    tone_requested = 1;
}

void feedback_init(void) {
    audio_running = 1;
    audio_thread_id = sceKernelCreateThread(
        "audio_feedback",
        audio_thread_func,
        0x10000100,   /* priority (default user) */
        0x4000,       /* 16KB stack */
        0,            /* attributes */
        0,            /* CPU affinity (any core) */
        NULL
    );
    if (audio_thread_id >= 0) {
        sceKernelStartThread(audio_thread_id, 0, NULL);
        printf("[FEEDBACK] Audio thread created: 0x%08X\n", audio_thread_id);
    } else {
        printf("[FEEDBACK] Failed to create audio thread: 0x%08X\n", audio_thread_id);
    }
}

void feedback_tone_estop_on(void) {
    /* Sharp 880Hz tone, 200ms */
    request_tone(880.0f, 880.0f, 200, 0);
}

void feedback_tone_estop_off(void) {
    /* Descending sweep 660->330Hz, 250ms */
    request_tone(660.0f, 330.0f, 250, 1);
}

void feedback_tone_alert(void) {
    /* Warning: 440Hz, 300ms */
    request_tone(440.0f, 440.0f, 300, 0);
}

void feedback_tone_ready(void) {
    if (ui_muted) return;
    /* Ascending sweep 330->660Hz, 200ms */
    request_tone(330.0f, 660.0f, 200, 1);
}

void feedback_tone_battery(void) {
    /* Descending chirp 600->300Hz, 350ms — battery threshold warning */
    request_tone(600.0f, 300.0f, 350, 1);
}

void feedback_tone_screenshot(void) {
    if (ui_muted) return;
    /* Short high click 1200Hz, 50ms — shutter sound */
    request_tone(1200.0f, 1200.0f, 50, 0);
}

void feedback_tone_speed(int preset) {
    if (ui_muted) return;
    /* Pitch mapped to speed level, 80ms */
    float freqs[] = { 440.0f, 660.0f, 880.0f };
    int idx = preset;
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;
    request_tone(freqs[idx], freqs[idx], 80, 0);
}

void feedback_tone_switch(void) {
    if (ui_muted) return;
    /* Two-tone ascending chirp 550->770Hz, 100ms */
    request_tone(550.0f, 770.0f, 100, 1);
}

void feedback_tone_delete(void) {
    if (ui_muted) return;
    /* Low descending tone 400->200Hz, 150ms */
    request_tone(400.0f, 200.0f, 150, 1);
}

void feedback_tone_save(void) {
    if (ui_muted) return;
    /* Ascending ding 500->800Hz, 100ms */
    request_tone(500.0f, 800.0f, 100, 1);
}

void feedback_tone_error(void) {
    if (ui_muted) return;
    /* Short low buzz 220Hz, 80ms */
    request_tone(220.0f, 220.0f, 80, 0);
}

void feedback_set_ui_mute(int mute) {
    ui_muted = mute ? 1 : 0;
}

int feedback_get_ui_mute(void) {
    return ui_muted;
}

void feedback_cleanup(void) {
    audio_running = 0;
    if (audio_thread_id >= 0) {
        sceKernelWaitThreadEnd(audio_thread_id, NULL, NULL);
        audio_thread_id = -1;
    }
}
