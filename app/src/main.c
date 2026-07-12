#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE    1024
#define PRIO_PROD     5
#define PRIO_CONS     5
#define EVENT_COUNT   8
#define PRODUCE_MS    200   /* producer fires every 200 ms */
#define POLL_MS       10    /* polling consumer checks every 10 ms */

/* ------------------------------------------------------------------ */
/*  Part A -- polling                                                   */
/* ------------------------------------------------------------------ */

static volatile bool event_flag;
static K_SEM_DEFINE(part_a_complete, 0, 1);
static K_SEM_DEFINE(part_b_complete, 0, 1);

/* Producer: sets a flag */
void producer_poll(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < EVENT_COUNT; i++) {
        k_msleep(PRODUCE_MS);
        event_flag = true;
        LOG_INF("[PROD-A] event %d produced at tick %u",
                i, k_uptime_get_32());
    }
}

/* Consumer: polls flag every POLL_MS */
void consumer_poll(void *p1, void *p2, void *p3)
{
    int received = 0;
    int wakeups  = 0;

    while (received < EVENT_COUNT) {
        k_msleep(POLL_MS);
        wakeups++;

        if (event_flag) {
            event_flag = false;
            received++;
            LOG_INF("[CONS-A] processed event %d (woke %d times so far)",
                    received, wakeups);
        }
    }

    LOG_INF("[CONS-A] total wake-ups: %d  for %d events  "
            "(~%d unnecessary wake-ups)",
            wakeups, EVENT_COUNT, wakeups - EVENT_COUNT);
    k_sem_give(&part_a_complete);
}

/* ------------------------------------------------------------------ */
/*  Part B -- semaphore                                                 */
/* ------------------------------------------------------------------ */

static K_SEM_DEFINE(event_sem, 0, EVENT_COUNT);

/* Producer: gives semaphore */
void producer_sem(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < EVENT_COUNT; i++) {
        k_msleep(PRODUCE_MS);
        k_sem_give(&event_sem);
        LOG_INF("[PROD-B] event %d produced at tick %u",
                i, k_uptime_get_32());
    }
}

/* Consumer: wakes exactly once per event */
void consumer_sem(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < EVENT_COUNT; i++) {
        k_sem_take(&event_sem, K_FOREVER);  /* blocks until signaled */
        LOG_INF("[CONS-B] processed event %d at tick %u",
                i, k_uptime_get_32());
    }

    LOG_INF("[CONS-B] total wake-ups: %d  for %d events  (zero waste)",
            EVENT_COUNT, EVENT_COUNT);
    k_sem_give(&part_b_complete);
}

/* ------------------------------------------------------------------ */
/*  Thread stacks                                                        */
/* ------------------------------------------------------------------ */

K_THREAD_STACK_DEFINE(prod_a_stk, STACK_SIZE);
K_THREAD_STACK_DEFINE(cons_a_stk, STACK_SIZE);
K_THREAD_STACK_DEFINE(prod_b_stk, STACK_SIZE);
K_THREAD_STACK_DEFINE(cons_b_stk, STACK_SIZE);

static struct k_thread prod_a, cons_a, prod_b, cons_b;

int main(void)
{
    LOG_INF("=== L2 Demo 3: Producer / Consumer Synchronization ===");

    /* --- Part A: polling --- */
    LOG_INF("--- Part A: POLLING consumer (every %d ms) ---", POLL_MS);
    event_flag = false;

    k_thread_create(&prod_a, prod_a_stk, STACK_SIZE, producer_poll,
                    NULL, NULL, NULL, PRIO_PROD, 0, K_NO_WAIT);
    k_thread_create(&cons_a, cons_a_stk, STACK_SIZE, consumer_poll,
                    NULL, NULL, NULL, PRIO_CONS, 0, K_NO_WAIT);

    k_sem_take(&part_a_complete, K_FOREVER);
    k_msleep(300);

    /* --- Part B: semaphore --- */
    LOG_INF("--- Part B: SEMAPHORE consumer (blocks until event) ---");

    k_thread_create(&prod_b, prod_b_stk, STACK_SIZE, producer_sem,
                    NULL, NULL, NULL, PRIO_PROD, 0, K_NO_WAIT);
    k_thread_create(&cons_b, cons_b_stk, STACK_SIZE, consumer_sem,
                    NULL, NULL, NULL, PRIO_CONS, 0, K_NO_WAIT);

    k_sem_take(&part_b_complete, K_FOREVER);

    LOG_INF("=== Summary ===");
    LOG_INF("Polling:   ~%d wake-ups for %d events",
            (PRODUCE_MS / POLL_MS) * EVENT_COUNT, EVENT_COUNT);
    LOG_INF("Semaphore: exactly %d wake-ups for %d events",
            EVENT_COUNT, EVENT_COUNT);

    return 0;
}

