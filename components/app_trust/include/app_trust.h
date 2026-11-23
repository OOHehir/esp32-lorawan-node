/* app_trust -- what this device believes about who it is talking to.
 *
 * PLAN §3 calls the trust anchor "the actual design fork", and it is the one decision
 * here that cannot be changed after devices ship: a device trusts what its firmware says
 * it trusts. That makes it a project-level choice rather than any one component's
 * business, which is why it lives in a component of its own with nothing else in it.
 *
 * It started inside app_ota, because OTA was the only thing making TLS connections. The
 * moment a second component needed the same anchor (app_mqtt, PLAN §8) that stopped
 * being tenable -- and ESP-IDF made the point concrete by refusing to build: EMBED_TXTFILES
 * names its generated stub after the file's basename, so two components embedding the
 * same certificate collide in ninja. The fix and the correct design turned out to be the
 * same thing.
 *
 * One anchor, one place, and no way for two call sites to quietly differ about it.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The private CA embedded at build time, NUL-terminated for mbedTLS's PEM parser.
 * NULL when this build trusts the public root bundle instead. */
const char *app_trust_ca_pem(void);

/* The bundle attach function, for config structs that take one. NULL when this build
 * pins a private CA; exactly one of the two is ever non-NULL. */
typedef esp_err_t (*app_trust_bundle_attach_fn)(void *conf);
app_trust_bundle_attach_fn app_trust_bundle_attach(void);

/* For logs and the status page -- "the embedded private CA" or "the IDF root certificate
 * bundle". Worth showing: a TLS failure looks like a network fault either way, and the
 * first useful question is which anchor the device is actually using. */
const char *app_trust_name(void);

#ifdef __cplusplus
}
#endif
