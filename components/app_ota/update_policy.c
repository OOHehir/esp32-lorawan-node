#include "update_policy.h"

#include <stdio.h>
#include <string.h>

static bool empty(const char *s) { return s == NULL || s[0] == '\0'; }

/* Local rather than strncasecmp(): that lives in <strings.h> and is POSIX, and this file
 * is deliberately dependency-free so the host tests can compile it as strict C11. */
static bool starts_with_ci(const char *s, const char *prefix)
{
    for (size_t i = 0; prefix[i]; i++) {
        char a = s[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z') { a = (char)(a - 'A' + 'a'); }
        if (b >= 'A' && b <= 'Z') { b = (char)(b - 'A' + 'a'); }
        if (a != b) {
            return false;
        }
    }
    return true;
}

const char *update_verdict_name(update_verdict_t v)
{
    switch (v) {
    case UPDATE_APPLY:         return "apply";
    case UPDATE_UP_TO_DATE:    return "up_to_date";
    case UPDATE_WRONG_PROJECT: return "wrong_project";
    case UPDATE_WRONG_CHIP:    return "wrong_chip";
    case UPDATE_INSECURE_URL:  return "insecure_url";
    case UPDATE_MALFORMED:     return "malformed";
    case UPDATE_QUARANTINED:   return "quarantined";
    }
    return "unknown";
}

update_verdict_t update_decide(const update_offer_t *offer, const update_device_t *dev,
                               char *reason, size_t reason_len)
{
    /* Write something into `reason` on every path, so a caller that only logs it can
     * never print an uninitialised buffer. */
    if (reason && reason_len) {
        reason[0] = '\0';
    }
#define SAY(...) do { if (reason && reason_len) snprintf(reason, reason_len, __VA_ARGS__); } while (0)

    if (!offer || !dev ||
        empty(offer->project) || empty(offer->chip) || empty(offer->version) ||
        empty(offer->url) ||
        empty(dev->project) || empty(dev->chip) || empty(dev->version)) {
        SAY("manifest is missing a required field");
        return UPDATE_MALFORMED;
    }

    /* Flashing an image built for another project bricks the device, and one NAS feeds a
     * mixed fleet from one directory tree -- a publish into the wrong path is an ordinary
     * mistake, not an attack. Check before trusting the layout. */
    if (strcmp(offer->project, dev->project) != 0) {
        SAY("manifest is for project \"%s\", this is \"%s\"", offer->project, dev->project);
        return UPDATE_WRONG_PROJECT;
    }
    if (strcmp(offer->chip, dev->chip) != 0) {
        SAY("manifest is for %s, this is %s", offer->chip, dev->chip);
        return UPDATE_WRONG_CHIP;
    }

    /* The manifest chooses the URL, so the manifest could quietly move the download to
     * plain HTTP. Refuse: the CA pinning that makes this whole design safe only applies
     * to a TLS connection. Case-insensitive, because a scheme is. */
    if (!starts_with_ci(offer->url, "https://")) {
        SAY("manifest url is not https");
        return UPDATE_INSECURE_URL;
    }

    /* Server-authoritative: *different* means update, not *newer*.
     *
     * publish.sh writes exactly one manifest per project/chip, so its version is the one
     * the server wants devices to run -- which makes a deliberate downgrade work, and
     * avoids parsing `git describe` output (v0.2.0, v0.2.0-3-gab18cdd, v0.2.0-dirty) as
     * if it were semver. Anti-rollback, when it is wanted, is an eFuse feature and a
     * separate decision (PLAN §4). */
    if (strcmp(offer->version, dev->version) == 0) {
        SAY("already running %s", dev->version);
        return UPDATE_UP_TO_DATE;
    }

    /* Checked last, and only against a version we are not already running: a device that
     * somehow ended up running a quarantined build should report "up to date", not
     * accuse itself.
     *
     * Without this the poll is a loop -- download 900 KB, boot, fail the self-test, roll
     * back, and do it all again six hours later, for as long as the broken image stays
     * published. Two attempts is enough to distinguish a bad image from a bad moment. */
    if (dev->max_failures > 0 && !empty(dev->failed_version) &&
        strcmp(offer->version, dev->failed_version) == 0 &&
        dev->failed_count >= dev->max_failures) {
        if (dev->forced) {
            SAY("%s failed %u times -- retrying anyway, you asked",
                offer->version, dev->failed_count);
        } else {
            SAY("%s failed to run here %u times -- not retrying automatically",
                offer->version, dev->failed_count);
            return UPDATE_QUARANTINED;
        }
    }

    SAY("%s -> %s", dev->version, offer->version);
    return UPDATE_APPLY;
#undef SAY
}
