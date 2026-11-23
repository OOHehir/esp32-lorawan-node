#include "lora_wait.h"

bool lora_wait(const lora_wait_io_t *io, uint32_t ms, uint32_t spin_tail_ms)
{
    const int64_t end_us = io->now_us(io->ctx) + (int64_t)ms * 1000;
    const int64_t spin_from_us = end_us - (int64_t)spin_tail_ms * 1000;
    while (io->now_us(io->ctx) < spin_from_us) {
        if (io->irq(io->ctx)) {
            return true;
        }
        io->sleep_tick(io->ctx);
    }
    while (io->now_us(io->ctx) < end_us) {
        if (io->irq(io->ctx)) {
            return true;
        }
    }
    return false;
}
