#include "shed_screen.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void shed_screen_age(int64_t age_s, char *out, int size)
{
    if (age_s < 60) {
        snprintf(out, size, "%llds", (long long)age_s);
    } else if (age_s < 3600) {
        snprintf(out, size, "%lldm", (long long)(age_s / 60));
    } else if (age_s < 86400) {
        snprintf(out, size, "%lldh", (long long)(age_s / 3600));
    } else {
        snprintf(out, size, "%lldd", (long long)(age_s / 86400));
    }
}

/* Formats one screen line, cutting it at the screen width. */
__attribute__((format(printf, 2, 3)))
static void line(char *dst, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, OLED_COLS + 1, fmt, ap);
    va_end(ap);
}

/* The bit's label when it is set, '-' when not. */
static char flag(uint8_t bits, int bit, char label)
{
    return (bits & (1u << bit)) ? label : '-';
}

static void page_status(const shed_screen_t *s, shed_screen_lines_t l)
{
    char age[12];

    switch (s->lora) {
    case SHED_LORA_JOINED:
        if (s->uplink_age_s < 0) {
            line(l[0], "LoRa OK");
        } else {
            shed_screen_age(s->uplink_age_s, age, sizeof age);
            line(l[0], "LoRa OK %s", age);
        }
        break;
    case SHED_LORA_JOINING:  line(l[0], "LoRa join.."); break;
    case SHED_LORA_NO_KEYS:  line(l[0], "LoRa nokeys"); break;
    case SHED_LORA_DISABLED: line(l[0], "LoRa off"); break;
    case SHED_LORA_FAULT:    line(l[0], "LoRa fault"); break;
    case SHED_LORA_ERROR:    line(l[0], "LoRa error"); break;
    }

    switch (s->wifi) {
    case SHED_WIFI_UNPROVISIONED: line(l[1], "WiFi none"); break;
    case SHED_WIFI_CONNECTING:    line(l[1], "WiFi conn.."); break;
    case SHED_WIFI_SETUP_AP:      line(l[1], "WiFi setup"); break;
    case SHED_WIFI_CONNECTED:     line(l[1], "WiFi %ddBm", s->wifi_rssi); break;
    }

    line(l[2], "In  %c %c %c", flag(s->inputs, 0, 'B'), flag(s->inputs, 1, '1'),
         flag(s->inputs, 2, '2'));
    line(l[3], "Out %c %c", flag(s->outputs, 0, '1'), flag(s->outputs, 1, '2'));
}

static void page_link(const shed_screen_t *s, shed_screen_lines_t l)
{
    char age[12];

    if (s->downlink_age_s < 0) {
        line(l[0], "Dn none");
        line(l[1], "%s", "");
    } else {
        shed_screen_age(s->downlink_age_s, age, sizeof age);
        line(l[0], "Dn %s %ddBm", age, s->downlink_rssi);
        line(l[1], "SNR %.1fdB", s->downlink_snr);
    }

    if (s->wifi != SHED_WIFI_CONNECTED) {
        line(l[2], "No IP");
        line(l[3], "%s", "");
        return;
    }
    /* An IPv4 address is up to 15 characters, so split it after the second dot. */
    const char *dot = strchr(s->ip, '.');
    dot = dot ? strchr(dot + 1, '.') : NULL;
    if (!dot) {
        line(l[2], "%s", s->ip);
        line(l[3], "%s", "");
        return;
    }
    line(l[2], "%.*s", (int)(dot - s->ip + 1), s->ip);
    line(l[3], "%s", dot + 1);
}

void shed_screen_format(const shed_screen_t *s, int page, shed_screen_lines_t lines)
{
    if (page == 0) {
        page_status(s, lines);
    } else {
        page_link(s, lines);
    }
}

bool shed_screen_lit(int64_t now_us, int64_t activity_us, uint32_t timeout_s)
{
    return timeout_s == 0 || now_us - activity_us < (int64_t)timeout_s * 1000000;
}

int shed_screen_page(int64_t now_us, int64_t woke_us)
{
    return (int)((now_us - woke_us) / (SHED_SCREEN_PAGE_S * INT64_C(1000000)) % SHED_SCREEN_PAGES);
}
