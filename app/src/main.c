#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/tracing/tracing.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

#define STACK_SIZE       2048
#define EVENT_PERIOD_MS   250
#define CPU_LOAD_US     60000
#define DEADLINE_MS        10

/* Set to 1 after proving that ready time causes the deadline miss. */
#ifndef FIX_WORKER_PRIORITY
#define FIX_WORKER_PRIORITY 1
#endif

#if FIX_WORKER_PRIORITY
#define WORKER_PRIORITY 3
#else
#define WORKER_PRIORITY 7
#endif

struct event_msg {
    uint32_t seq;
    uint32_t ready_ms;
};

K_MSGQ_DEFINE(event_queue, sizeof(struct event_msg), 2, 4);
K_SEM_DEFINE(load_start, 0, 1);
K_SEM_DEFINE(demo_start, 0, 3);

static void producer_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint32_t seq = 0;

    k_sem_take(&demo_start, K_FOREVER);

    while (true) {
        struct event_msg msg = {
            .seq = seq,
            .ready_ms = k_uptime_get_32(),
        };

        LOG_INF("[PRODUCER] event_ready seq=%u", msg.seq);

        /* Make both threads ready before scheduler selection resumes. */
        k_sched_lock();
        int ret = k_msgq_put(&event_queue, &msg, K_NO_WAIT);
        k_sem_give(&load_start);
        sys_trace_named_event("event_ready", msg.seq,
                              k_msgq_num_used_get(&event_queue));
        k_sched_unlock();

        if (ret != 0) {
            LOG_ERR("[PRODUCER] queue full, seq=%u", msg.seq);
        }

        seq++;
        k_msleep(EVENT_PERIOD_MS);
    }
}

static void cpu_load_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_sem_take(&demo_start, K_FOREVER);

    while (true) {
        k_sem_take(&load_start, K_FOREVER);

        LOG_INF("[CPU_LOAD] running for %u ms", CPU_LOAD_US / 1000U);
        k_busy_wait(CPU_LOAD_US);
        LOG_INF("[CPU_LOAD] done");
    }
}

static void worker_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint32_t deadline_misses = 0;

    k_sem_take(&demo_start, K_FOREVER);

    while (true) {
        struct event_msg msg;
        int ret = k_msgq_get(&event_queue, &msg, K_FOREVER);

        if (ret != 0) {
            LOG_ERR("[WORKER] receive failed: %d", ret);
            continue;
        }

        uint32_t latency_ms = k_uptime_get_32() - msg.ready_ms;

        sys_trace_named_event("event_done", msg.seq, latency_ms);

        if (latency_ms > DEADLINE_MS) {
            deadline_misses++;
            LOG_WRN("[WORKER] deadline_miss count=%u seq=%u latency=%ums",
                    deadline_misses, msg.seq, latency_ms);
        } else {
            LOG_INF("[WORKER] event_done seq=%u latency=%ums",
                    msg.seq, latency_ms);
        }
    }
}

K_THREAD_DEFINE(producer, STACK_SIZE, producer_fn,
                NULL, NULL, NULL, 2, 0, 0);

K_THREAD_DEFINE(cpu_load, STACK_SIZE, cpu_load_fn,
                NULL, NULL, NULL, 4, 0, 0);

K_THREAD_DEFINE(worker, STACK_SIZE, worker_fn,
                NULL, NULL, NULL, WORKER_PRIORITY, 0, 0);

int main(void)
{
    LOG_INF("=== L6 Demo 2: Scheduling Delay Trace ===");
    LOG_INF("producer priority=2, cpu_load priority=4");
    LOG_INF("worker priority=%d, deadline=%dms",
            WORKER_PRIORITY, DEADLINE_MS);

#if FIX_WORKER_PRIORITY
    LOG_INF("Corrected: worker runs before cpu_load");
#else
    LOG_INF("Failure: cpu_load runs before worker");
#endif

    k_sem_give(&demo_start);
    k_sem_give(&demo_start);
    k_sem_give(&demo_start);

    return 0;
}
