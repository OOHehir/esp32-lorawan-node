/* The wait RadioLib's sleep callback performs, with the clock, the radio's IRQ line and the
 * scheduler passed in so the host tests can drive it with a fake clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t (*now_us)(void *ctx);
    bool (*irq)(void *ctx);         /* true while the radio's DIO0 is high */
    void (*sleep_tick)(void *ctx);  /* block for one scheduler tick */
    void *ctx;
} lora_wait_io_t;

/* Waits up to ms milliseconds: sleeps tick by tick, then spins the last spin_tail_ms for
 * precision. Returns true, early, as soon as irq() reads true; false after the full wait.
 *
 * RadioLib uses the same callback for the TxDone wait, where it expects an early return
 * on the interrupt; ignoring the IRQ stamps the end of the uplink late and every receive
 * window opens late. */
bool lora_wait(const lora_wait_io_t *io, uint32_t ms, uint32_t spin_tail_ms);

#ifdef __cplusplus
}
#endif
