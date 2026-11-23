#include "test.h"
#include "lora_wait.h"

/* A fake clock: a scheduler tick advances it 1 ms, every clock read during the spin
 * advances it 10 us, and the IRQ goes high at irq_at_us (never if negative). */
typedef struct {
    int64_t now_us;
    int64_t irq_at_us;
    int ticks;
} fake_t;

static int64_t fake_now(void *ctx)
{
    fake_t *f = ctx;
    f->now_us += 10;
    return f->now_us;
}

static bool fake_irq(void *ctx)
{
    fake_t *f = ctx;
    return f->irq_at_us >= 0 && f->now_us >= f->irq_at_us;
}

static void fake_tick(void *ctx)
{
    fake_t *f = ctx;
    f->now_us += 1000;
    f->ticks++;
}

static bool run(fake_t *f, uint32_t ms)
{
    const lora_wait_io_t io = {fake_now, fake_irq, fake_tick, f};
    return lora_wait(&io, ms, 2);
}

static void returns_within_a_tick_of_the_irq(void)
{
    fake_t f = {.now_us = 0, .irq_at_us = 50000};
    bool woke = run(&f, 1000);
    CHECK(woke, "IRQ did not end the wait");
    CHECK(f.now_us < 51100, "returned at %lld us, IRQ was at 50000 us", (long long)f.now_us);
}

static void waits_the_full_time_without_an_irq(void)
{
    fake_t f = {.now_us = 0, .irq_at_us = -1};
    bool woke = run(&f, 1000);
    CHECK(!woke, "reported an IRQ that never happened");
    CHECK(f.now_us >= 1000000, "returned early, at %lld us", (long long)f.now_us);
    CHECK(f.now_us < 1001100, "overshot, returned at %lld us", (long long)f.now_us);
}

/* Each sleep loop costs a 1 ms tick plus a 10 us clock read, so 998 ms of sleeping is 989
 * ticks. Spinning the whole wait instead would be 0 ticks and starve the idle task. */
static void sleeps_all_but_the_spin_tail(void)
{
    fake_t f = {.now_us = 0, .irq_at_us = -1};
    run(&f, 1000);
    CHECK(f.ticks >= 985 && f.ticks <= 998, "slept %d ticks of a 1000 ms wait", f.ticks);
}

static void irq_already_high_returns_at_once(void)
{
    fake_t f = {.now_us = 0, .irq_at_us = 0};
    CHECK(run(&f, 1000), "IRQ high at entry not seen");
    CHECK_INT(f.ticks, 0);
}

void test_lora_wait(void)
{
    SUITE("lora_wait");
    returns_within_a_tick_of_the_irq();
    waits_the_full_time_without_an_irq();
    sleeps_all_but_the_spin_tail();
    irq_already_high_returns_at_once();
}
