#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

/*
 * PART A: Uncomment this define to demonstrate the fault.
 * Expiry will call k_sleep - assertion triggers immediately.
 */
#define BLOCK_IN_EXPIRY 

static void work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    LOG_INF("[HANDLER] context=%s  tick=%u",
            k_thread_name_get(k_current_get()),
            k_uptime_get_32());
}

K_WORK_DEFINE(my_work, work_handler);

/* ------------------------------------------------------------------ */
/*  Timer expiry - runs in ISR context                                */
/* ------------------------------------------------------------------ */

static void timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    /* Confirm we're in ISR context */
    LOG_INF("[EXPIRY]  in_isr=%d  tick=%u",
            k_is_in_isr() ? 1 : 0,
            k_uptime_get_32());

#ifdef BLOCK_IN_EXPIRY
    /*
     * PART A: This is the bug. k_sleep blocks the caller.
     * Blocking in ISR context triggers assertion failure.
     * With CONFIG_ASSERT=y: "ASSERTION FAIL" printed, system halts.
     * This is intentional - we're demonstrating what NOT to do.
     */
    LOG_INF("[EXPIRY]  About to call k_sleep - this will crash!");
    k_sleep(K_MSEC(10));
#else
    /*
     * PART B: Correct. Submit work and return immediately.
     * Handler runs later in workqueue thread context.
     */
    int ret = k_work_submit(&my_work);
    if (ret < 0) {
        LOG_ERR("[EXPIRY]  k_work_submit failed: %d", ret);
    }
#endif
}

K_TIMER_DEFINE(my_timer, timer_expiry, NULL);


int main(void)
{
    LOG_INF("=== L3 Demo 1: Timer Expiry Context ===");

#ifdef BLOCK_IN_EXPIRY
    LOG_INF("PART A: blocking in expiry - expect assertion failure");
#else
    LOG_INF("PART B: correct - expiry submits work, returns immediately");
    LOG_INF("Observe: [EXPIRY] in_isr=1, [HANDLER] context=sysworkq");
#endif

    LOG_INF("Starting one-shot timer (300ms)...");
    k_timer_start(&my_timer, K_MSEC(300), K_NO_WAIT);
    k_msleep(500);

    LOG_INF("Starting periodic timer (400ms interval)...");
    k_timer_start(&my_timer, K_MSEC(400), K_MSEC(400));
    k_msleep(2200);
    k_timer_stop(&my_timer);

    LOG_INF("Done.");
    return 0;
}

