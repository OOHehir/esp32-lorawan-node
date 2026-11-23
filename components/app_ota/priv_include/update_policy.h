/* Should this device install the image the manifest advertises?
 *
 * Free of ESP-IDF and of cJSON so it can be tested on the host (test/host/). This is
 * where a silent bug does the most damage: get it wrong in one direction and every
 * device concludes it is already up to date and never updates again; get it wrong in the
 * other and they flash an image built for a different project.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    UPDATE_APPLY = 0,        /* versions differ and everything checks out */
    UPDATE_UP_TO_DATE,       /* the server is offering what we are already running */
    UPDATE_WRONG_PROJECT,
    UPDATE_WRONG_CHIP,
    UPDATE_INSECURE_URL,     /* manifest points somewhere other than https */
    UPDATE_MALFORMED,        /* a required field is missing or empty */
    UPDATE_QUARANTINED,      /* this exact version has already failed to boot here */
} update_verdict_t;

/* What the manifest says. */
typedef struct {
    const char *project;
    const char *chip;
    const char *version;
    const char *url;
} update_offer_t;

/* What this device is, and what it remembers. */
typedef struct {
    const char *project;
    const char *chip;
    const char *version;

    /* The one version known to have failed here, and how many times -- PLAN §4's
     * "don't retry a version that has already failed twice". Empty when nothing is
     * quarantined. One slot is enough: a new failure replaces the record, because the
     * version that just broke is the one worth remembering. */
    const char *failed_version;
    unsigned    failed_count;
    unsigned    max_failures;   /* 0 disables the quarantine entirely */

    /* A human pressed "Check for updates". A forced check ignores the quarantine, so a
     * version that failed because the *server* was wrong -- a truncated upload, a
     * mispublished file -- is not a permanent dead end reachable only by factory reset.
     * Automatic polls still honour it, which is what stops a fleet re-downloading a
     * broken image every six hours. */
    bool        forced;
} update_device_t;

/* Either pointer may be NULL, and so may any string in them; that is treated as
 * malformed rather than trusted. `reason` receives a short human-readable explanation
 * and is what the status page and the log show. */
update_verdict_t update_decide(const update_offer_t *offer, const update_device_t *dev,
                               char *reason, size_t reason_len);

const char *update_verdict_name(update_verdict_t v);
