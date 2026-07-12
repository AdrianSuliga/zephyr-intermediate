#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_A 5
#define PRIO_B 5

void thread_a_fn(void *p1, void *p2, void *p3)
{
    int step = 0;

    while (1) {
        LOG_INF("[A] step %d  tick=%u", step++, k_uptime_get_32());
        k_msleep(200);
    }
}

void thread_b_fn(void *p1, void *p2, void *p3)
{
    int step = 0;

    while (1) {
        LOG_INF("[B] step %d  tick=%u", step++, k_uptime_get_32());
        k_msleep(300);
    }
}

K_THREAD_DEFINE(thread_a, STACK_SIZE, thread_a_fn,
                NULL, NULL, NULL, PRIO_A, 0, 0);
K_THREAD_DEFINE(thread_b, STACK_SIZE, thread_b_fn,
                NULL, NULL, NULL, PRIO_B, 0, 0);

int main(void)
{
    LOG_INF("=== L1 Demo 1: Thread Interleaving ===");
    LOG_INF("Thread A: priority %d, sleeps 200ms", PRIO_A);
    LOG_INF("Thread B: priority %d, sleeps 300ms", PRIO_B);
    return 0;
}

