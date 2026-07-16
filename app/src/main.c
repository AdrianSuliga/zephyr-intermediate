#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

/*
 * Part 1: set to 1 to trigger an intentional stack overflow.
 * Part 2: set to 0 to run the Thread Analyzer.
 */
#define TRIGGER_STACK_OVERFLOW 1

#if TRIGGER_STACK_OVERFLOW
#define WORKER_STACK_SIZE 768
#else
#define WORKER_STACK_SIZE 1536
#endif

#define WORKER_PRIORITY 5

K_SEM_DEFINE(workload_done, 0, 1);
K_SEM_DEFINE(worker_release, 0, 1);

/*
 * Every recursive call creates another 192-byte stack frame.
 * noinline keeps the recursive calls visible to the compiler.
 */
__attribute__((noinline))
static uint32_t consume_stack(uint32_t depth)
{
    volatile uint8_t frame[192];

    for (size_t i = 0; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)(depth + i);
    }

    if (depth == 0U) {
        /*
         * This context switch gives the stack sentinel
         * an opportunity to check the current thread.
         */
        k_yield();
        return frame[0];
    }

    uint32_t value = consume_stack(depth - 1U);

    /*
     * Access the frame after recursion.
     * This prevents early reuse of the stack space.
     */
    return value + frame[depth % sizeof(frame)];
}

static void worker_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("[WORKER] stack size=%d bytes", WORKER_STACK_SIZE);

#if TRIGGER_STACK_OVERFLOW
    LOG_WRN("[WORKER] starting the intentional overflow");
    LOG_WRN("[WORKER] expect a fatal stack error");

    /*
     * Nine stack frames require more space than allocated.
     * Zephyr should report a stack overflow.
     */
    (void)consume_stack(8U);

    LOG_ERR("[WORKER] overflow was not detected");
#else
    LOG_INF("[WORKER] running the representative workload");

    uint32_t checksum = 0U;

    for (int sample = 0; sample < 8; sample++) {
        checksum += consume_stack(2U);

        LOG_INF("[WORKER] sample=%d checksum=%u",
                sample, checksum);

        k_msleep(40);
    }

    /*
     * Keep this thread alive while main scans its stack.
     */
    k_sem_give(&workload_done);
    k_sem_take(&worker_release, K_FOREVER);

    LOG_INF("[WORKER] done");
#endif
}

K_THREAD_DEFINE(worker_thread, WORKER_STACK_SIZE, worker_fn,
                NULL, NULL, NULL, WORKER_PRIORITY, 0, 0);

int main(void)
{
    LOG_INF("=== L5 Demo 1: Stack Usage ===");

#if TRIGGER_STACK_OVERFLOW
    LOG_INF("Part 1: stack sentinel failure");
    LOG_INF("Set TRIGGER_STACK_OVERFLOW to 0 for Part 2");

    k_sleep(K_FOREVER);
#else
    LOG_INF("Part 2: workload and Thread Analyzer");

    k_sem_take(&workload_done, K_FOREVER);

    LOG_INF("--- Thread Analyzer report ---");

    thread_analyzer_print(0);

    LOG_INF("Compare used bytes with allocated stack sizes");
    LOG_INF("Keep headroom for interrupts and rare code paths");

    k_sem_give(&worker_release);
#endif

    return 0;
}
