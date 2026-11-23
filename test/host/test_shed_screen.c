#include "oled_fb.h"
#include "shed_screen.h"
#include "test.h"

static shed_screen_t joined_node(void)
{
    shed_screen_t s = {
        .lora = SHED_LORA_JOINED,
        .uplink_age_s = 15,
        .downlink_age_s = 120,
        .downlink_rssi = -97,
        .downlink_snr = 7.5f,
        .wifi = SHED_WIFI_CONNECTED,
        .ip = "192.168.1.20",
        .wifi_rssi = -67,
        .inputs = 0x02,
        .outputs = 0x01,
    };
    return s;
}

static void status_page_shows_lora_wifi_and_io(void)
{
    shed_screen_t s = joined_node();
    shed_screen_lines_t l;

    shed_screen_format(&s, 0, l);

    CHECK_STR(l[0], "LoRa OK 15s");
    CHECK_STR(l[1], "WiFi -67dBm");
    CHECK_STR(l[2], "In  - 1 -");
    CHECK_STR(l[3], "Out 1 -");
}

static void link_page_splits_ip_after_second_dot(void)
{
    shed_screen_t s = joined_node();
    shed_screen_lines_t l;

    shed_screen_format(&s, 1, l);

    CHECK_STR(l[0], "Dn 2m -97dBm");
    CHECK_STR(l[1], "SNR 7.5dB");
    CHECK_STR(l[2], "192.168.");
    CHECK_STR(l[3], "1.20");
}

static void fresh_node_says_none_yet(void)
{
    shed_screen_t s = joined_node();
    s.uplink_age_s = -1;
    s.downlink_age_s = -1;
    shed_screen_lines_t l;

    shed_screen_format(&s, 0, l);
    CHECK_STR(l[0], "LoRa OK");

    shed_screen_format(&s, 1, l);
    CHECK_STR(l[0], "Dn none");
    CHECK_STR(l[1], "");
}

static void each_lora_failure_has_its_own_label(void)
{
    shed_screen_t s = joined_node();
    shed_screen_lines_t l;

    s.lora = SHED_LORA_JOINING;
    shed_screen_format(&s, 0, l);
    CHECK_STR(l[0], "LoRa join..");

    s.lora = SHED_LORA_NO_KEYS;
    shed_screen_format(&s, 0, l);
    CHECK_STR(l[0], "LoRa nokeys");

    s.lora = SHED_LORA_DISABLED;
    shed_screen_format(&s, 0, l);
    CHECK_STR(l[0], "LoRa off");

    s.lora = SHED_LORA_FAULT;
    shed_screen_format(&s, 0, l);
    CHECK_STR(l[0], "LoRa fault");

    s.lora = SHED_LORA_ERROR;
    shed_screen_format(&s, 0, l);
    CHECK_STR(l[0], "LoRa error");
}

static void wifi_without_address_shows_state_and_no_ip(void)
{
    shed_screen_t s = joined_node();
    shed_screen_lines_t l;

    s.wifi = SHED_WIFI_UNPROVISIONED;
    shed_screen_format(&s, 0, l);
    CHECK_STR(l[1], "WiFi none");

    s.wifi = SHED_WIFI_CONNECTING;
    shed_screen_format(&s, 0, l);
    CHECK_STR(l[1], "WiFi conn..");

    s.wifi = SHED_WIFI_SETUP_AP;
    shed_screen_format(&s, 0, l);
    CHECK_STR(l[1], "WiFi setup");

    shed_screen_format(&s, 1, l);
    CHECK_STR(l[2], "No IP");
    CHECK_STR(l[3], "");
}

static void all_inputs_and_outputs_active_show_their_labels(void)
{
    shed_screen_t s = joined_node();
    s.inputs = 0x07;
    s.outputs = 0x03;
    shed_screen_lines_t l;

    shed_screen_format(&s, 0, l);

    CHECK_STR(l[2], "In  B 1 2");
    CHECK_STR(l[3], "Out 1 2");
}

static void overlong_line_is_cut_to_screen_width(void)
{
    shed_screen_t s = joined_node();
    s.downlink_age_s = 100 * 86400;
    s.downlink_rssi = -120;
    shed_screen_lines_t l;

    shed_screen_format(&s, 1, l);

    CHECK_STR(l[0], "Dn 100d -120");
}

static void age_switches_unit_at_each_boundary(void)
{
    char a[12];
    shed_screen_age(59, a, sizeof a);
    CHECK_STR(a, "59s");
    shed_screen_age(60, a, sizeof a);
    CHECK_STR(a, "1m");
    shed_screen_age(3599, a, sizeof a);
    CHECK_STR(a, "59m");
    shed_screen_age(3600, a, sizeof a);
    CHECK_STR(a, "1h");
    shed_screen_age(86399, a, sizeof a);
    CHECK_STR(a, "23h");
    shed_screen_age(86400, a, sizeof a);
    CHECK_STR(a, "1d");
}

static void screen_goes_dark_when_timeout_expires(void)
{
    CHECK(shed_screen_lit(59999999, 0, 60), "dark before the timeout");
    CHECK(!shed_screen_lit(60000000, 0, 60), "still lit at the timeout");
}

static void zero_timeout_keeps_screen_lit(void)
{
    CHECK(shed_screen_lit(INT64_C(1000000000000), 0, 0), "dark with timeout 0");
}

static void pages_alternate_every_four_seconds_from_wake(void)
{
    CHECK_INT(shed_screen_page(10000000, 10000000), 0);
    CHECK_INT(shed_screen_page(13999999, 10000000), 0);
    CHECK_INT(shed_screen_page(14000000, 10000000), 1);
    CHECK_INT(shed_screen_page(18000000, 10000000), 0);
}

static void text_is_drawn_double_size_after_the_margin(void)
{
    oled_fb_t fb;
    oled_fb_clear(&fb);

    oled_fb_text(&fb, 1, "!");

    /* '!' is column 2 of the glyph, 0x2f: rows 0-3 and 5. Doubled: rows 0-7 and 10-11,
     * so 0xff in the upper page and 0x0c in the lower, in pixel columns 4+4 and 4+5. */
    CHECK_INT(fb.px[2 * 128 + 8], 0xff);
    CHECK_INT(fb.px[2 * 128 + 9], 0xff);
    CHECK_INT(fb.px[3 * 128 + 8], 0x0c);
    CHECK_INT(fb.px[3 * 128 + 9], 0x0c);
    CHECK_INT(fb.px[2 * 128 + 7], 0x00);
    CHECK_INT(fb.px[2 * 128 + 10], 0x00);
    CHECK_INT(fb.px[0 * 128 + 8], 0x00);   /* row 0 untouched */
}

static void unprintable_character_is_drawn_as_question_mark(void)
{
    oled_fb_t want, got;
    oled_fb_clear(&want);
    oled_fb_clear(&got);
    oled_fb_text(&want, 0, "?");

    oled_fb_text(&got, 0, "\x01");

    CHECK(memcmp(want.px, got.px, sizeof want.px) == 0, "\\x01 not drawn as '?'");
}

static void text_row_is_cleared_before_drawing(void)
{
    oled_fb_t fb, blank;
    oled_fb_clear(&fb);
    oled_fb_clear(&blank);
    oled_fb_text(&fb, 3, "WWWWWWWWWWWW");

    oled_fb_text(&fb, 3, "");

    CHECK(memcmp(fb.px, blank.px, sizeof fb.px) == 0, "old text left behind");
}

static void out_of_range_row_is_ignored(void)
{
    oled_fb_t fb, blank;
    oled_fb_clear(&fb);
    oled_fb_clear(&blank);

    oled_fb_text(&fb, -1, "x");
    oled_fb_text(&fb, OLED_ROWS, "x");

    CHECK(memcmp(fb.px, blank.px, sizeof fb.px) == 0, "out-of-range row drew pixels");
}

void test_shed_screen(void)
{
    SUITE("shed_screen");
    status_page_shows_lora_wifi_and_io();
    link_page_splits_ip_after_second_dot();
    fresh_node_says_none_yet();
    each_lora_failure_has_its_own_label();
    wifi_without_address_shows_state_and_no_ip();
    all_inputs_and_outputs_active_show_their_labels();
    overlong_line_is_cut_to_screen_width();
    age_switches_unit_at_each_boundary();
    screen_goes_dark_when_timeout_expires();
    zero_timeout_keeps_screen_lit();
    pages_alternate_every_four_seconds_from_wake();
    text_is_drawn_double_size_after_the_margin();
    unprintable_character_is_drawn_as_question_mark();
    text_row_is_cleared_before_drawing();
    out_of_range_row_is_ignored();
}
