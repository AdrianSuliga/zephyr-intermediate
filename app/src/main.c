#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

/* ================================================================== */
/*  Shared message type                                               */
/* ================================================================== */

struct sensor_msg {
    uint32_t timestamp_ms;
    int32_t value;
    uint8_t seq;
};

/* ================================================================== */
/*  Part 1 - Thread-to-thread pipeline                                */
/* ================================================================== */

#define P1_QUEUE_DEPTH 6
#define P1_COUNT       12

K_MSGQ_DEFINE(p1_q, sizeof(struct sensor_msg), P1_QUEUE_DEPTH, 4);

static K_SEM_DEFINE(p1_prod_done, 0, 1);
static K_SEM_DEFINE(p1_cons_done, 0, 1);

static volatile bool p1_prod_finished;

static void p1_producer(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "p1_prod");

    for (int i = 0; i < P1_COUNT; i++) {
        struct sensor_msg msg = {
            .timestamp_ms = k_uptime_get_32(),
            .value = 100 + i,
            .seq = (uint8_t)i,
        };

        int ret = k_msgq_put(&p1_q, &msg, K_MSEC(200));
        if (ret == 0) {
            LOG_INF("[P1-PROD] sent seq=%u val=%d q=%u/%d",
                    msg.seq,
                    msg.value,
                    k_msgq_num_used_get(&p1_q),
                    P1_QUEUE_DEPTH);
        } else {
            LOG_WRN("[P1-PROD] put failed ret=%d", ret);
        }

        k_msleep(50);
    }

    p1_prod_finished = true;
    LOG_INF("[P1-PROD] done");
    k_sem_give(&p1_prod_done);
}

static void p1_consumer(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "p1_cons");

    while (true) {
        struct sensor_msg msg = {0};

        int ret = k_msgq_get(&p1_q, &msg, K_MSEC(300));
        if (ret != 0) {
            if (p1_prod_finished && k_msgq_num_used_get(&p1_q) == 0) {
                break;
            }

            LOG_WRN("[P1-CONS] timeout waiting for message");
            continue;
        }

        uint32_t latency = k_uptime_get_32() - msg.timestamp_ms;

        LOG_INF("[P1-CONS] got seq=%u val=%d q=%u/%d latency=%ums",
                msg.seq,
                msg.value,
                k_msgq_num_used_get(&p1_q),
                P1_QUEUE_DEPTH,
                latency);

        k_msleep(60);
    }

    LOG_INF("[P1-CONS] done");
    k_sem_give(&p1_cons_done);
}

/* ================================================================== */
/*  Part 2 - Timer expiry to thread via k_msgq                        */
/* ================================================================== */

#define P2_QUEUE_DEPTH 4
#define P2_WANT        12

K_MSGQ_DEFINE(p2_q, sizeof(struct sensor_msg), P2_QUEUE_DEPTH, 4);

static K_SEM_DEFINE(p2_cons_done, 0, 1);

static atomic_t p2_isr_sent;
static atomic_t p2_isr_dropped;

static void p2_timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    struct sensor_msg msg = {
        .timestamp_ms = k_uptime_get_32(),
        .value = 42,
        .seq = (uint8_t)atomic_get(&p2_isr_sent),
    };

    /*
     * Timer expiry runs in ISR context.
     * K_NO_WAIT is mandatory here.
     */
    int ret = k_msgq_put(&p2_q, &msg, K_NO_WAIT);
    if (ret == 0) {
        atomic_inc(&p2_isr_sent);
    } else {
        atomic_inc(&p2_isr_dropped);
    }
}

K_TIMER_DEFINE(p2_timer, p2_timer_expiry, NULL);

static void p2_consumer(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "p2_cons");

    int received = 0;

    while (received < P2_WANT) {
        struct sensor_msg msg;
        int ret = k_msgq_get(&p2_q, &msg, K_MSEC(600));

        if (ret != 0) {
            LOG_WRN("[P2-CONS] timeout waiting for timer message");
            continue;
        }

        received++;

        LOG_INF("[P2-CONS] got seq=%u val=%d q=%u/%d (%d/%d)",
                msg.seq,
                msg.value,
                k_msgq_num_used_get(&p2_q),
                P2_QUEUE_DEPTH,
                received,
                P2_WANT);

        int drops = (int)atomic_set(&p2_isr_dropped, 0);
        if (drops > 0) {
            LOG_WRN("[P2-CONS] ISR drops=%d (queue full, K_NO_WAIT)", drops);
        }

        k_msleep(120);
    }

    LOG_INF("[P2-CONS] done");
    k_sem_give(&p2_cons_done);
}

/* ================================================================== */
/*  Part 3 - Slow consumer and overflow strategy                      */
/* ================================================================== */

#define P3_QUEUE_DEPTH 6
#define P3_COUNT       30

K_MSGQ_DEFINE(p3_q, sizeof(struct sensor_msg), P3_QUEUE_DEPTH, 4);

static K_SEM_DEFINE(p3_prod_done, 0, 1);
static K_SEM_DEFINE(p3_cons_done, 0, 1);

static volatile bool p3_prod_finished;

static void p3_producer(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "p3_prod");

    int sent = 0;
    int dropped = 0;

    for (int i = 0; i < P3_COUNT; i++) {
        struct sensor_msg msg = {
            .timestamp_ms = k_uptime_get_32(),
            .value = i,
            .seq = (uint8_t)i,
        };

        /*
         * Drop-oldest strategy.
         * If the queue is full, purge old data and retry.
         *
         * Experiment:
         * Replace K_NO_WAIT with K_FOREVER to see blocking behaviour.
         */
        while (k_msgq_put(&p3_q, &msg, K_NO_WAIT) != 0) {
            dropped += k_msgq_num_used_get(&p3_q);

            LOG_WRN("[P3-PROD] q full, purging old data before seq=%u",
                    msg.seq);

            k_msgq_purge(&p3_q);
        }

        sent++;

        LOG_INF("[P3-PROD] sent seq=%u q=%u/%d",
                msg.seq,
                k_msgq_num_used_get(&p3_q),
                P3_QUEUE_DEPTH);

        k_msleep(50);
    }

    p3_prod_finished = true;

    LOG_INF("[P3-PROD] done sent=%d dropped_old=%d", sent, dropped);
    k_sem_give(&p3_prod_done);
}

static void p3_consumer(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "p3_cons");

    int received = 0;

    while (true) {
        struct sensor_msg msg;
        int ret = k_msgq_get(&p3_q, &msg, K_MSEC(500));

        if (ret != 0) {
            if (p3_prod_finished && k_msgq_num_used_get(&p3_q) == 0) {
                break;
            }

            LOG_WRN("[P3-CONS] timeout");
            continue;
        }

        received++;

        LOG_INF("[P3-CONS] got seq=%u val=%d q=%u/%d (slow)",
                msg.seq,
                msg.value,
                k_msgq_num_used_get(&p3_q),
                P3_QUEUE_DEPTH);

        k_msleep(200);
    }

    LOG_INF("[P3-CONS] done received=%d", received);
    k_sem_give(&p3_cons_done);
}

/* ================================================================== */
/*  Runtime threads                                                   */
/* ================================================================== */

K_THREAD_STACK_DEFINE(p1_prod_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(p1_cons_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(p2_cons_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(p3_prod_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(p3_cons_stack, STACK_SIZE);

static struct k_thread p1_prod_thread;
static struct k_thread p1_cons_thread;
static struct k_thread p2_cons_thread;
static struct k_thread p3_prod_thread;
static struct k_thread p3_cons_thread;

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void)
{
    LOG_INF("=== L4 Demo 1: Message Queue Pipeline ===");
    LOG_INF("sizeof(sensor_msg)=%u", sizeof(struct sensor_msg));

    LOG_INF("\n--- Part 1: thread-to-thread pipeline ---");
    k_msgq_purge(&p1_q);
    p1_prod_finished = false;

    k_thread_create(&p1_prod_thread, p1_prod_stack,
                    K_THREAD_STACK_SIZEOF(p1_prod_stack), p1_producer, 
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    /*
    * Let producer get ahead.
    * This makes the queue buffer real messages instead of direct handoff.
    */
    k_msleep(180);

    k_thread_create(&p1_cons_thread,
                    p1_cons_stack,
                    K_THREAD_STACK_SIZEOF(p1_cons_stack), p1_consumer,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    k_sem_take(&p1_prod_done, K_FOREVER);
    k_sem_take(&p1_cons_done, K_FOREVER);

    LOG_INF("\n--- Part 2: timer expiry to thread via k_msgq ---");
    LOG_INF("Timer: 30ms. Consumer: 120ms. Queue depth: %d.", P2_QUEUE_DEPTH);

    k_msgq_purge(&p2_q);
    atomic_set(&p2_isr_sent, 0);
    atomic_set(&p2_isr_dropped, 0);

    k_thread_create(&p2_cons_thread, p2_cons_stack,
                    K_THREAD_STACK_SIZEOF(p2_cons_stack), p2_consumer,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    k_timer_start(&p2_timer, K_MSEC(30), K_MSEC(30));

    k_sem_take(&p2_cons_done, K_FOREVER);
    k_timer_stop(&p2_timer);

    LOG_INF("[P2] total sent=%d remaining_drops=%d",
            (int)atomic_get(&p2_isr_sent),
            (int)atomic_get(&p2_isr_dropped));

    LOG_INF("\n--- Part 3: slow consumer and drop-oldest strategy ---");

    k_msgq_purge(&p3_q);
    p3_prod_finished = false;

    k_thread_create(&p3_cons_thread, p3_cons_stack,
                    K_THREAD_STACK_SIZEOF(p3_cons_stack), p3_consumer,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    k_thread_create(&p3_prod_thread, p3_prod_stack,
                    K_THREAD_STACK_SIZEOF(p3_prod_stack), p3_producer,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    k_sem_take(&p3_prod_done, K_FOREVER);
    k_sem_take(&p3_cons_done, K_FOREVER);

    k_msleep(200);

    LOG_INF("\n=== Demo complete ===");

    return 0;
}


