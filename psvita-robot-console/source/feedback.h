#ifndef FEEDBACK_H
#define FEEDBACK_H

/* Audio tones (non-blocking, plays on dedicated thread) */
void feedback_init(void);
void feedback_tone_estop_on(void);   /* Sharp high tone: e-stop engaged */
void feedback_tone_estop_off(void);  /* Descending tone: e-stop released */
void feedback_tone_alert(void);      /* Warning tone: connection lost */
void feedback_tone_ready(void);      /* Ascending tone: startup complete */
void feedback_tone_battery(void);    /* Descending chirp: battery threshold crossed */
void feedback_tone_screenshot(void); /* Short click: screenshot captured */
void feedback_tone_speed(int preset);/* Pitch mapped to speed: 0=low 1=mid 2=high */
void feedback_tone_switch(void);     /* Two-tone chirp: robot switched */
void feedback_tone_delete(void);     /* Low descending: item deleted */
void feedback_tone_save(void);       /* Ascending ding: config saved */
void feedback_tone_error(void);      /* Short buzz: invalid input / rejected action */
void feedback_set_ui_mute(int mute); /* Mute non-critical UI tones (safety tones unaffected) */
int  feedback_get_ui_mute(void);
void feedback_cleanup(void);

#endif
