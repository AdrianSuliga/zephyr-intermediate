#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_COOP  (-1)
#define PRIO_HIGH    3
#define PRIO_LOW     7

void coop_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[COOP] starting - will run 3 steps without yielding");

    for (int i = 0; i < 3; i++) {
        k_busy_wait(40000);   
        LOG_INF("[COOP] step %d/3 - still holding CPU  tick=%u",
                i + 1, k_uptime_get_32());
    }

    LOG_INF("[COOP] yielding now - HIGH and LOW can run");
    k_yield();

    LOG_INF("[COOP] done");
}

void high_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[HIGH] entering main loop - will preempt LOW whenever Ready");

    for (int i = 0; i < 10; i++) {
        LOG_INF("[HIGH] step %d  tick=%u", i, k_uptime_get_32());
        k_msleep(100);
    }

    LOG_INF("[HIGH] done");
}

void low_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[LOW] started");

    for (int i = 0; i < 10; i++) {
        LOG_INF("[LOW] step %d  tick=%u", i, k_uptime_get_32());

        k_msleep(100);
    }

    LOG_INF("[LOW] done");
}

K_THREAD_DEFINE(t_coop, STACK_SIZE, coop_fn, NULL, NULL, NULL, PRIO_COOP, 0, 0);
K_THREAD_DEFINE(t_high, STACK_SIZE, high_fn, NULL, NULL, NULL, PRIO_HIGH, 0, 0);
K_THREAD_DEFINE(t_low,  STACK_SIZE, low_fn,  NULL, NULL, NULL, PRIO_LOW,  0, 0);

int main(void)
{
    LOG_INF("=== L1 Demo 2: Scheduling Competition ===");
    LOG_INF("COOP prio=%d (cooperative)  HIGH prio=%d  LOW prio=%d",
            PRIO_COOP, PRIO_HIGH, PRIO_LOW);
    return 0;
}
