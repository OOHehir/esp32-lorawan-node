#include "app_trust.h"

#include "sdkconfig.h"

#if CONFIG_APP_TRUST_PRIVATE_CA
extern const char ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
#else
#include "esp_crt_bundle.h"
#endif

const char *app_trust_ca_pem(void)
{
#if CONFIG_APP_TRUST_PRIVATE_CA
    return ca_cert_pem_start;
#else
    return NULL;
#endif
}

app_trust_bundle_attach_fn app_trust_bundle_attach(void)
{
#if CONFIG_APP_TRUST_PRIVATE_CA
    return NULL;
#else
    return esp_crt_bundle_attach;
#endif
}

const char *app_trust_name(void)
{
#if CONFIG_APP_TRUST_PRIVATE_CA
    return "the embedded private CA";
#else
    return "the IDF root certificate bundle";
#endif
}
