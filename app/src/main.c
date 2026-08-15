#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l2_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 1024
#define INCREMENT 5000000

#define PRIORITY 5

static int counter = 0;

K_SEM_DEFINE(counter_finished, 0, 2);
K_MUTEX_DEFINE(counter_access);

void thread_fn(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < INCREMENT; ++i) {
        k_mutex_lock(&counter_access, K_FOREVER);
        ++counter;
        k_mutex_unlock(&counter_access);
    }

    k_sem_give(&counter_finished);
}

K_THREAD_DEFINE(t_fn_1, STACK_SIZE, thread_fn, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(t_fn_2, STACK_SIZE, thread_fn, NULL, NULL, NULL, PRIORITY, 0, 0);

int main()
{
    k_sem_take(&counter_finished, K_FOREVER);
    k_sem_take(&counter_finished, K_FOREVER);

    LOG_INF("Expected: %d", 2 * INCREMENT);
    LOG_INF("Actual:   %d", counter);
    LOG_INF("===================");
    LOG_INF("Error: %d", 2 * INCREMENT - counter);

    return 0;
}
