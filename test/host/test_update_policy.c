#include "test.h"
#include "update_policy.h"

/* The realistic baseline: what this project actually publishes. */
#define P "esp-ota"
#define C "esp32s3"
#define U "https://192.168.1.10:8443/fw/esp-ota/esp32s3/v0.3.0/esp-ota.bin"

static update_verdict_t decide(const char *mp, const char *mc, const char *mv,
                               const char *mu, const char *rv)
{
    char reason[112];
    const update_offer_t offer = { .project = mp, .chip = mc, .version = mv, .url = mu };
    const update_device_t dev = { .project = P, .chip = C, .version = rv,
                                  .max_failures = 2 };
    return update_decide(&offer, &dev, reason, sizeof(reason));
}

/* The same, with a quarantine record in place. */
static update_verdict_t decide_bad(const char *mv, const char *rv,
                                   const char *bad, unsigned count, bool forced)
{
    char reason[112];
    const update_offer_t offer = { .project = P, .chip = C, .version = mv, .url = U };
    const update_device_t dev = { .project = P, .chip = C, .version = rv,
                                  .failed_version = bad, .failed_count = count,
                                  .max_failures = 2, .forced = forced };
    return update_decide(&offer, &dev, reason, sizeof(reason));
}

void test_update_policy(void)
{
    char reason[112];
    const update_offer_t ok_offer = { .project = P, .chip = C, .version = "v0.3.0", .url = U };
    const update_device_t ok_dev  = { .project = P, .chip = C, .version = "v0.2.0",
                                      .max_failures = 2 };

    SUITE("update_decide -- the normal cases");

    CHECK_INT(decide(P, C, "v0.3.0", U, "v0.2.0"), UPDATE_APPLY);
    CHECK_INT(decide(P, C, "v0.2.0", U, "v0.2.0"), UPDATE_UP_TO_DATE);

    /* Server-authoritative: a lower version on the server is still what the server wants
     * these devices running, so it applies. Deliberate, and the reason it is safe to
     * avoid parsing `git describe` output as semver. */
    CHECK_INT(decide(P, C, "v0.1.0", U, "v0.2.0"), UPDATE_APPLY);

    /* git describe strings that are NOT semver, and must not be treated as equal just
     * because they share a prefix. */
    CHECK_INT(decide(P, C, "v0.2.0-3-gab18cdd", U, "v0.2.0"), UPDATE_APPLY);
    CHECK_INT(decide(P, C, "v0.2.0", U, "v0.2.0-dirty"), UPDATE_APPLY);
    CHECK_INT(decide(P, C, "v0.2.0-dirty", U, "v0.2.0-dirty"), UPDATE_UP_TO_DATE);

    SUITE("update_decide -- refusals that prevent a brick");

    /* One NAS feeds a mixed fleet from one directory tree, so publishing into the wrong
     * path is an ordinary mistake. Flashing the result is not recoverable over the air. */
    CHECK_INT(decide("other-project", C, "v0.3.0", U, "v0.2.0"), UPDATE_WRONG_PROJECT);
    CHECK_INT(decide(P, "esp32c6", "v0.3.0", U, "v0.2.0"), UPDATE_WRONG_CHIP);

    /* Case matters for identifiers, and "ESP32S3" is not the target name IDF reports. */
    CHECK_INT(decide(P, "ESP32S3", "v0.3.0", U, "v0.2.0"), UPDATE_WRONG_CHIP);

    SUITE("update_decide -- the manifest must not downgrade the transport");

    /* The manifest chooses the download URL, so it could quietly move the image to plain
     * HTTP. CA pinning only protects a TLS connection. */
    CHECK_INT(decide(P, C, "v0.3.0", "http://192.168.1.10/x.bin", "v0.2.0"),
              UPDATE_INSECURE_URL);
    CHECK_INT(decide(P, C, "v0.3.0", "ftp://nas/x.bin", "v0.2.0"), UPDATE_INSECURE_URL);
    CHECK_INT(decide(P, C, "v0.3.0", "/fw/x.bin", "v0.2.0"), UPDATE_INSECURE_URL);
    /* A scheme is case-insensitive; rejecting this one would be a false alarm. */
    CHECK_INT(decide(P, C, "v0.3.0", "HTTPS://nas:8443/x.bin", "v0.2.0"), UPDATE_APPLY);
    /* "https" must not match as a substring somewhere other than the start. */
    CHECK_INT(decide(P, C, "v0.3.0", "http://evil/?u=https://nas/x.bin", "v0.2.0"),
              UPDATE_INSECURE_URL);

    SUITE("update_decide -- missing fields are malformed, not trusted");

    CHECK_INT(decide(NULL, C, "v0.3.0", U, "v0.2.0"), UPDATE_MALFORMED);
    CHECK_INT(decide(P, NULL, "v0.3.0", U, "v0.2.0"), UPDATE_MALFORMED);
    CHECK_INT(decide(P, C, NULL, U, "v0.2.0"), UPDATE_MALFORMED);
    CHECK_INT(decide(P, C, "v0.3.0", NULL, "v0.2.0"), UPDATE_MALFORMED);
    CHECK_INT(decide(P, C, "", U, "v0.2.0"), UPDATE_MALFORMED);
    CHECK_INT(decide(P, C, "v0.3.0", "", "v0.2.0"), UPDATE_MALFORMED);
    CHECK_INT(decide(P, C, "v0.3.0", U, ""), UPDATE_MALFORMED);

    /* An empty manifest -- what a half-written publish leaves behind -- must be refused
     * rather than read as "no update". */
    CHECK_INT(decide("", "", "", "", "v0.2.0"), UPDATE_MALFORMED);

    /* Neither struct may be dereferenced blind. */
    CHECK_INT(update_decide(NULL, &ok_dev, reason, sizeof(reason)), UPDATE_MALFORMED);
    CHECK_INT(update_decide(&ok_offer, NULL, reason, sizeof(reason)), UPDATE_MALFORMED);
    CHECK_INT(update_decide(NULL, NULL, reason, sizeof(reason)), UPDATE_MALFORMED);

    SUITE("update_decide -- a version that has already failed here");

    /* Below the threshold it is still worth another go: one failure can be a bad moment
     * rather than a bad image. */
    CHECK_INT(decide_bad("v0.3.0", "v0.2.0", "v0.3.0", 1, false), UPDATE_APPLY);
    CHECK_INT(decide_bad("v0.3.0", "v0.2.0", "v0.3.0", 2, false), UPDATE_QUARANTINED);
    CHECK_INT(decide_bad("v0.3.0", "v0.2.0", "v0.3.0", 9, false), UPDATE_QUARANTINED);

    /* A human asking overrides it -- otherwise a version broken by a bad *upload* is a
     * dead end reachable only by factory reset. */
    CHECK_INT(decide_bad("v0.3.0", "v0.2.0", "v0.3.0", 9, true), UPDATE_APPLY);

    /* The record names one version. Any other version is unaffected by it. */
    CHECK_INT(decide_bad("v0.4.0", "v0.2.0", "v0.3.0", 5, false), UPDATE_APPLY);
    /* And a version string that merely starts the same is a different version. */
    CHECK_INT(decide_bad("v0.3.0-1-gdeadbee", "v0.2.0", "v0.3.0", 5, false), UPDATE_APPLY);

    /* No record, or an empty one, quarantines nothing. */
    CHECK_INT(decide_bad("v0.3.0", "v0.2.0", NULL, 0, false), UPDATE_APPLY);
    CHECK_INT(decide_bad("v0.3.0", "v0.2.0", "", 7, false), UPDATE_APPLY);

    /* A device somehow *running* a quarantined version reports up to date rather than
     * accusing itself -- the quarantine is about what to install next. */
    CHECK_INT(decide_bad("v0.3.0", "v0.3.0", "v0.3.0", 5, false), UPDATE_UP_TO_DATE);

    /* max_failures 0 disables the quarantine entirely. */
    {
        const update_offer_t offer = { .project = P, .chip = C, .version = "v0.3.0", .url = U };
        const update_device_t dev = { .project = P, .chip = C, .version = "v0.2.0",
                                      .failed_version = "v0.3.0", .failed_count = 99,
                                      .max_failures = 0 };
        CHECK_INT(update_decide(&offer, &dev, reason, sizeof(reason)), UPDATE_APPLY);
    }

    /* A quarantine must never rescue a manifest that is wrong in a more serious way:
     * the brick-preventing refusals still win. */
    {
        const update_offer_t offer = { .project = "other", .chip = C,
                                       .version = "v0.3.0", .url = U };
        const update_device_t dev = { .project = P, .chip = C, .version = "v0.2.0",
                                      .failed_version = "v0.3.0", .failed_count = 9,
                                      .max_failures = 2, .forced = true };
        CHECK_INT(update_decide(&offer, &dev, reason, sizeof(reason)), UPDATE_WRONG_PROJECT);
    }

    SUITE("update_decide -- the reason string");

    /* Callers log this unconditionally, so it must be written on every path. */
    update_verdict_t vs[] = { UPDATE_APPLY, UPDATE_UP_TO_DATE, UPDATE_WRONG_PROJECT,
                              UPDATE_WRONG_CHIP, UPDATE_INSECURE_URL, UPDATE_MALFORMED,
                              UPDATE_QUARANTINED };
    for (size_t i = 0; i < sizeof(vs) / sizeof(vs[0]); i++) {
        CHECK(update_verdict_name(vs[i])[0] != '\0', "verdict %d has no name", (int)vs[i]);
    }

    memset(reason, 'x', sizeof(reason));
    update_decide(&ok_offer, &ok_dev, reason, sizeof(reason));
    CHECK(strchr(reason, '\0') != NULL && reason[0] != 'x', "apply left no reason");

    memset(reason, 'x', sizeof(reason));
    update_decide(NULL, NULL, reason, sizeof(reason));
    CHECK(reason[0] != 'x', "malformed left no reason");

    memset(reason, 'x', sizeof(reason));
    {
        const update_offer_t offer = { .project = P, .chip = C, .version = "v0.3.0", .url = U };
        const update_device_t dev = { .project = P, .chip = C, .version = "v0.2.0",
                                      .failed_version = "v0.3.0", .failed_count = 4,
                                      .max_failures = 2 };
        update_decide(&offer, &dev, reason, sizeof(reason));
        CHECK(reason[0] != 'x', "quarantined left no reason");
    }

    /* A tiny buffer must truncate, not overflow. */
    {
        char tiny[8];
        memset(tiny, 'x', sizeof(tiny));
        const update_offer_t offer = { .project = "a-very-long-project-name", .chip = C,
                                       .version = "v0.3.0", .url = U };
        update_decide(&offer, &ok_dev, tiny, sizeof(tiny));
        CHECK(strlen(tiny) < sizeof(tiny), "wrote past a small reason buffer");
    }

    /* A NULL buffer must simply be tolerated. */
    CHECK_INT(update_decide(&ok_offer, &ok_dev, NULL, 0), UPDATE_APPLY);
}
