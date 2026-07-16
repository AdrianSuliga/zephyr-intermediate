#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

/*
 * Experiment 1:
 * Change LOG_LEVEL_DBG to LOG_LEVEL_INF.
 * Rebuild and compare the ROM reports.
 */
#define APP_LOG_LEVEL LOG_LEVEL_DBG

/*
 * Experiment 2:
 * Reduce this from 1024 to 512 after measuring stack usage.
 */
#define PROCESSOR_STACK_SIZE 1024

/*
 * Experiment 3:
 * Set to 1 to add a 1024-byte queue buffer.
 */
#define ENABLE_AUDIT_QUEUE 0

LOG_MODULE_REGISTER(demo, APP_LOG_LEVEL);

#define SAMPLE_COUNT         12
#define SENSOR_QUEUE_DEPTH    8
#define PRODUCER_STACK_SIZE 1024
#define HEALTH_STACK_SIZE   1024

/* ================================================================== */
/*  Messages and communication objects                                */
/* ================================================================== */

struct sensor_sample {
    uint32_t seq;
    int32_t value;
    uint32_t timestamp_ms;
};

struct processed_sample {
    uint32_t seq;
    int32_t value;
    uint32_t latency_ms;
};

K_MSGQ_DEFINE(sensor_queue, sizeof(struct sensor_sample), SENSOR_QUEUE_DEPTH, 4);

#if ENABLE_AUDIT_QUEUE

struct audit_record {
    uint8_t bytes[64];
};

/*
 * Sixteen messages multiplied by 64 bytes equals 1024 bytes.
 */
K_MSGQ_DEFINE(audit_queue, sizeof(struct audit_record), 16, 4);

#endif

static void result_listener_cb(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(result_listener, result_listener_cb);

ZBUS_CHAN_DEFINE(result_chan, struct processed_sample,
                 NULL, NULL, ZBUS_OBSERVERS(result_listener),
                 ZBUS_MSG_INIT(
                     .seq = 0,
                     .value = 0,
                     .latency_ms = 0));

K_SEM_DEFINE(demo_done, 0, 3);

/* ================================================================== */
/*  Synchronous zbus listener                                         */
/* ================================================================== */

static void result_listener_cb(const struct zbus_channel *chan)
{
    const struct processed_sample *result = zbus_chan_const_msg(chan);

    LOG_DBG("[RESULT] seq=%u value=%d latency=%ums",
        result->seq, result->value, result->latency_ms);

    LOG_DBG("[RESULT] listener runs in publisher context");
}

/* ================================================================== */
/*  Producer                                                          */
/* ================================================================== */

static void producer_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (uint32_t seq = 0; seq < SAMPLE_COUNT; seq++) {
        struct sensor_sample sample = {
            .seq = seq,
            .value = 100 + (int32_t)seq,
            .timestamp_ms = k_uptime_get_32(),
        };

        LOG_DBG("[PRODUCER] acquired seq=%u value=%d timestamp=%u",
            sample.seq, sample.value, sample.timestamp_ms);

        int ret = k_msgq_put(&sensor_queue, &sample, K_MSEC(100));

        if (ret != 0) {
            LOG_WRN("[PRODUCER] queue full, dropped seq=%u", seq);
        } else {
            LOG_DBG("[PRODUCER] queued seq=%u used=%u/%u", 
                seq, k_msgq_num_used_get(&sensor_queue), SENSOR_QUEUE_DEPTH);
        }

#if ENABLE_AUDIT_QUEUE
        struct audit_record record = { 0 };

        record.bytes[0] = (uint8_t)seq;

        /*
         * Nothing consumes these records.
         * The queue exists to demonstrate its RAM cost.
         */
        (void)k_msgq_put(&audit_queue, &record, K_NO_WAIT);
#endif

        k_msleep(50);
    }

    LOG_INF("[PRODUCER] done");
    k_sem_give(&demo_done);
}

/* ================================================================== */
/*  Processor                                                         */
/* ================================================================== */

static void processor_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        struct sensor_sample sample;

        int ret = k_msgq_get(&sensor_queue, &sample, K_FOREVER);

        if (ret != 0) {
            LOG_ERR("[PROCESSOR] receive failed: %d", ret);
            break;
        }

        struct processed_sample result = {
            .seq = sample.seq,
            .value = sample.value * 2,
            .latency_ms = k_uptime_get_32() - sample.timestamp_ms,
        };

        LOG_DBG("[PROCESSOR] seq=%u input=%d output=%d",
            sample.seq, sample.value, result.value);

        LOG_DBG("[PROCESSOR] publishing on zbus");

        ret = zbus_chan_pub(&result_chan, &result, K_MSEC(100));

        if (ret != 0) {
            LOG_WRN("[PROCESSOR] publish failed: %d", ret);
        }

        /*
         * The processor is slower than the producer.
         * This makes queue buildup visible.
         */
        k_msleep(70);
    }

    LOG_INF("[PROCESSOR] done");
    k_sem_give(&demo_done);
}

/* ================================================================== */
/*  Health monitor                                                    */
/* ================================================================== */

static void health_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (int i = 0; i < 6; i++) {
        k_msleep(120);

        uint32_t used = k_msgq_num_used_get(&sensor_queue);

        LOG_INF("[HEALTH] sensor_queue=%u/%u",
            used, SENSOR_QUEUE_DEPTH);

        LOG_DBG("[HEALTH] queue has %u free slots", SENSOR_QUEUE_DEPTH - used);

#if ENABLE_AUDIT_QUEUE
        LOG_INF("[HEALTH] audit_queue=%u/16", k_msgq_num_used_get(&audit_queue));
#endif
    }

    LOG_INF("[HEALTH] done");
    k_sem_give(&demo_done);
}

/* ================================================================== */
/*  Threads                                                           */
/* ================================================================== */

K_THREAD_DEFINE(producer_thread, PRODUCER_STACK_SIZE, producer_fn,
                NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(processor_thread, PROCESSOR_STACK_SIZE, processor_fn,
                NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(health_thread, HEALTH_STACK_SIZE, health_fn,
                NULL, NULL, NULL, 6, 0, 0);

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void)
{
    LOG_INF("=== L5 Demo 2: Resource Budget Audit ===");

    LOG_INF("producer stack=%d bytes", PRODUCER_STACK_SIZE);

    LOG_INF("processor stack=%d bytes", PROCESSOR_STACK_SIZE);

    LOG_INF("health stack=%d bytes", HEALTH_STACK_SIZE);

    LOG_INF("sensor queue=%u bytes",
        (uint32_t)(sizeof(struct sensor_sample) * SENSOR_QUEUE_DEPTH));

#if ENABLE_AUDIT_QUEUE
    LOG_INF("optional audit queue=1024 bytes");
#else
    LOG_INF("optional audit queue=disabled");
#endif

    k_sem_take(&demo_done, K_FOREVER);
    k_sem_take(&demo_done, K_FOREVER);
    k_sem_take(&demo_done, K_FOREVER);

    LOG_INF("Run ram_report and rom_report");
    LOG_INF("Change one option and rebuild");

    return 0;
}