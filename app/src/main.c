#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

#define STACK_SIZE 2048

/* Set to 1 after diagnosing the circular wait. */
#ifndef FIX_LOCK_ORDER
#define FIX_LOCK_ORDER 0
#endif

K_MUTEX_DEFINE(mutex_a);
K_MUTEX_DEFINE(mutex_b);

K_SEM_DEFINE(start_workers, 0, 2);
K_SEM_DEFINE(first_locks_taken, 0, 2);
K_SEM_DEFINE(request_second_lock, 0, 2);

static atomic_t completed_iterations;

#if FIX_LOCK_ORDER
static void do_corrected_iteration(const char *name)
{
    LOG_INF("[%s] locking mutex_a", name);
    k_mutex_lock(&mutex_a, K_FOREVER);

    LOG_INF("[%s] locking mutex_b", name);
    k_mutex_lock(&mutex_b, K_FOREVER);

    atomic_inc(&completed_iterations);
    LOG_INF("[%s] completed=%d", name,
            (int)atomic_get(&completed_iterations));

    k_mutex_unlock(&mutex_b);
    k_mutex_unlock(&mutex_a);
}
#endif

static void worker_a_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_sem_take(&start_workers, K_FOREVER);

#if FIX_LOCK_ORDER
    for (int i = 0; i < 3; i++) {
        do_corrected_iteration("A");
        k_msleep(20);
    }
#else
    LOG_INF("[A] locking mutex_a");
    k_mutex_lock(&mutex_a, K_FOREVER);
    LOG_INF("[A] mutex_a acquired");

    k_sem_give(&first_locks_taken);
    k_sem_take(&request_second_lock, K_FOREVER);

    LOG_INF("[A] waiting forever for mutex_b");
    k_mutex_lock(&mutex_b, K_FOREVER);
#endif
}

static void worker_b_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_sem_take(&start_workers, K_FOREVER);

#if FIX_LOCK_ORDER
    for (int i = 0; i < 3; i++) {
        do_corrected_iteration("B");
        k_msleep(20);
    }
#else
    LOG_INF("[B] locking mutex_b");
    k_mutex_lock(&mutex_b, K_FOREVER);
    LOG_INF("[B] mutex_b acquired");

    k_sem_give(&first_locks_taken);
    k_sem_take(&request_second_lock, K_FOREVER);

    LOG_INF("[B] waiting forever for mutex_a");
    k_mutex_lock(&mutex_a, K_FOREVER);
#endif
}

/* A named function gives the live demo a stable GDB breakpoint. */
__attribute__((noinline))
void health_check(void)
{
    int completed = (int)atomic_get(&completed_iterations);

    if (completed == 0) {
        LOG_WRN("[HEALTH] no progress, completed=%d", completed);
    } else {
        LOG_INF("[HEALTH] progress confirmed, completed=%d", completed);
    }
}

static void health_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (true) {
        k_msleep(1000);
        health_check();
    }
}

K_THREAD_DEFINE(worker_a, STACK_SIZE, worker_a_fn,
                NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(worker_b, STACK_SIZE, worker_b_fn,
                NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(health, STACK_SIZE, health_fn,
                NULL, NULL, NULL, 6, 0, 0);

int main(void)
{
    LOG_INF("=== L6 Demo 1: Mutex Deadlock ===");

#if FIX_LOCK_ORDER
    LOG_INF("Corrected: both threads use mutex_a -> mutex_b");
#else
    LOG_INF("Failure: threads use opposite lock order");
    LOG_INF("Use kernel thread list, then inspect both threads in GDB");
#endif

    k_sem_give(&start_workers);
    k_sem_give(&start_workers);

#if !FIX_LOCK_ORDER
    k_sem_take(&first_locks_taken, K_FOREVER);
    k_sem_take(&first_locks_taken, K_FOREVER);

    LOG_INF("Both first locks are held; requesting second locks");
    k_sem_give(&request_second_lock);
    k_sem_give(&request_second_lock);
#endif

    return 0;
}
