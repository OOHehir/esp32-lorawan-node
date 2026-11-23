/* What the OLED shows, as text. Kept free of ESP-IDF so the host tests can check it.
 *
 * Two pages of 4 x 12 characters, alternating:
 *
 *   LoRa OK 15s      Dn 2m -97dBm
 *   WiFi -67dBm      SNR 7.5dB
 *   In  - 1 -        192.168.
 *   Out 1 -          1.20
 *
 * Ages are time since the event: s, m, h, then d.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "oled_fb.h"

#define SHED_SCREEN_PAGES  2
#define SHED_SCREEN_PAGE_S 4

typedef enum {
    SHED_LORA_JOINING,
    SHED_LORA_JOINED,
    SHED_LORA_NO_KEYS,     /* DevEUI or AppKey unset */
    SHED_LORA_DISABLED,    /* compiled out */
    SHED_LORA_FAULT,       /* SX1276 or LoRaWAN stack failed to start */
    SHED_LORA_ERROR,       /* anything else; the log says what */
} shed_lora_state_t;

typedef enum {
    SHED_WIFI_UNPROVISIONED,
    SHED_WIFI_CONNECTING,
    SHED_WIFI_SETUP_AP,
    SHED_WIFI_CONNECTED,
} shed_wifi_state_t;

typedef struct {
    shed_lora_state_t lora;
    int64_t uplink_age_s;      /* -1 = none yet */
    int64_t downlink_age_s;    /* -1 = none yet */
    int downlink_rssi;         /* dBm */
    float downlink_snr;        /* dB */
    shed_wifi_state_t wifi;
    const char *ip;            /* when connected */
    int wifi_rssi;             /* dBm, when connected */
    uint8_t inputs;            /* bit0 button, bit1 input 1, bit2 input 2 */
    uint8_t outputs;           /* bit0 output 1, bit1 output 2 */
} shed_screen_t;

typedef char shed_screen_lines_t[OLED_ROWS][OLED_COLS + 1];

/* Fills the lines of page 0 or 1. */
void shed_screen_format(const shed_screen_t *s, int page, shed_screen_lines_t lines);

/* Writes an age such as "45s", "12m", "3h" or "9d" into out. */
void shed_screen_age(int64_t age_s, char *out, int size);

/* True while the screen should stay lit: activity_us is the last button press or state
 * change, timeout_s 0 keeps it on for good. */
bool shed_screen_lit(int64_t now_us, int64_t activity_us, uint32_t timeout_s);

/* The page to show, counting from the moment the screen woke so it opens on page 0. */
int shed_screen_page(int64_t now_us, int64_t woke_us);
