#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l1_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_COOP (-1)
#define PRIO_HIGH   3
#define PRIO_MED    5
#define PRIO_LOW    7

void thread_coop_fn(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < 5; ++i) {
        LOG_INF("T_COOP busy work, tick=%u", k_uptime_get_32());
        k_busy_wait(40000);
    }

    k_yield();
}

void thread_high_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_HIGH running, tick=%u", k_uptime_get_32());
        k_msleep(100);
    }
}

void thread_med_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_MED running, tick=%u", k_uptime_get_32());
        k_msleep(200);
    }
}

void thread_low_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_LOW running, tick=%u", k_uptime_get_32());
        k_msleep(300);
    }
}

K_THREAD_DEFINE(t_coop_fn, STACK_SIZE, thread_coop_fn, NULL, NULL, NULL, PRIO_COOP, 0, 0);
K_THREAD_DEFINE(t_high_fn, STACK_SIZE, thread_high_fn, NULL, NULL, NULL, PRIO_HIGH, 0, 0);
K_THREAD_DEFINE(t_med_fn, STACK_SIZE, thread_med_fn, NULL, NULL, NULL, PRIO_MED, 0, 0);
K_THREAD_DEFINE(t_low_fn, STACK_SIZE, thread_low_fn, NULL, NULL, NULL, PRIO_LOW, 0, 0);
