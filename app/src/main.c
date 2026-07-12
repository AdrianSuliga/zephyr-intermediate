#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

/* ================================================================== */
/*  Part 1 - Bottom-half                                              */
/* ================================================================== */

static void bh_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    /* Runs in workqueue thread - can do anything */
    LOG_INF("[BH-HANDLER] context=%s  tick=%u",
            k_thread_name_get(k_current_get()),
            k_uptime_get_32());
    /* Simulate some processing that would be illegal in ISR */
    k_msleep(10);
}

K_WORK_DEFINE(bh_work, bh_handler);

static void producer_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (int i = 0; i < 5; i++) {
        k_msleep(150);
        LOG_INF("[PRODUCER] submitting work from context=%s",
                k_thread_name_get(k_current_get()));
        int ret = k_work_submit(&bh_work);
        if (ret < 0) {
            LOG_ERR("[PRODUCER] submit failed: %d", ret);
        }
    }
}

K_THREAD_DEFINE(producer, STACK_SIZE, producer_fn, NULL, NULL, NULL, 5, 0, 0);

/* ================================================================== */
/*  Part 2 - Debounce                                                 */
/* ================================================================== */

static void button_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    LOG_INF("[DEBOUNCE-HANDLER] fired at tick=%u  (single call after burst)",
            k_uptime_get_32());
}

K_WORK_DELAYABLE_DEFINE(button_work, button_handler);

static void simulate_bounce(void)
{
    LOG_INF("[DEBOUNCE] --- simulating 10 rapid button events ---");
    for (int i = 0; i < 10; i++) {
        LOG_INF("[DEBOUNCE] event %d  tick=%u", i, k_uptime_get_32());
        /*
         * k_work_reschedule: always resets the delay.
         * Handler fires 50ms after the LAST call.
         * "Last event wins" - this is debounce.
         *
         * Try changing to k_work_schedule to see "first event wins":
         * the handler fires 50ms after the FIRST call and ignores
         * all subsequent events.
         */
        k_work_reschedule(&button_work, K_MSEC(50));
        k_msleep(4);   /* 4ms between bounces - total burst ~40ms */
    }
    LOG_INF("[DEBOUNCE] burst done - handler should fire ~50ms from now");
}

/* ================================================================== */
/*  Part 3 - Periodic: self-rescheduling vs k_timer                  */
/* ================================================================== */

static int selfreschedule_count;

static void selfreschedule_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    static uint32_t previous_tick = 0;
    uint32_t current_tick = k_uptime_get_32();
    uint32_t delta = current_tick - previous_tick;
    if (previous_tick == 0) {
        delta = 0;   /* first invocation */
    }
    previous_tick = current_tick;

    selfreschedule_count++;
    LOG_INF("[SELFRESCH] invocation=%d  tick=%u  delta=%u",
            selfreschedule_count, current_tick, delta);

    /*
     * 5ms of fake work - makes drift visible on native_sim.
     * Self-rescheduling period = 200ms + 5ms = ~205ms.
     */
    k_busy_wait(5000);

    if (selfreschedule_count < 6) {
        /* Schedule AFTER this handler finishes - includes drift */
        k_work_reschedule(dwork, K_MSEC(200));
    }
}

K_WORK_DELAYABLE_DEFINE(selfreschedule_work, selfreschedule_handler);

/* k_timer version for strict-periodicity comparison */
static int timer_count;

static void periodic_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    static uint32_t previous_tick = 0;
    uint32_t current_tick = k_uptime_get_32();
    uint32_t delta = current_tick - previous_tick;
    if (previous_tick == 0) {
        delta = 0;   /* first invocation */
    }
    previous_tick = current_tick;
    
    timer_count++;
    LOG_INF("[TIMER-RESCH] invocation=%d  tick=%u  delta=%d",
            timer_count, current_tick, delta);
    /* Same 5ms fake work - but timer fires on fixed schedule */
    k_busy_wait(5000);
}

K_WORK_DEFINE(periodic_work, periodic_work_handler);

static void periodic_timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    /* Expiry in ISR: just submit work, don't do the 5ms work here */
    k_work_submit(&periodic_work);
}

K_TIMER_DEFINE(periodic_timer, periodic_timer_expiry, NULL);

/* ================================================================== */
/*  Main - runs parts sequentially                                    */
/* ================================================================== */

int main(void)
{
    LOG_INF("=== L3 Demo 2: Workqueue Patterns ===");

    /* --- Part 1: Bottom-half --- */
    LOG_INF("--- Part 1: Bottom-half (5 submissions) ---");
    /* producer thread is already running; wait for it to finish */
    k_msleep(1100);

    /* --- Part 2: Debounce --- */
    LOG_INF("--- Part 2: Debounce ---");
    simulate_bounce();
    k_msleep(400);   /* wait for handler + margin */

    /* --- Part 3: Periodic --- */
    LOG_INF("--- Part 3a: Self-rescheduling (expect ~205ms intervals) ---");
    selfreschedule_count = 0;
    k_work_schedule(&selfreschedule_work, K_MSEC(200));
    k_msleep(1400);   /* 6 * 205ms ~= 1230ms + margin */

    LOG_INF("--- Part 3b: k_timer + k_work_submit (expect ~200ms intervals) ---");
    timer_count = 0;
    k_timer_start(&periodic_timer, K_MSEC(200), K_MSEC(200));
    k_msleep(1400);
    k_timer_stop(&periodic_timer);

    LOG_INF("=== Complete ===");

    return 0;
}
