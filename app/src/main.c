#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(l4_task1, LOG_LEVEL_DBG);

#define PRIORITY 5
#define STACK_SIZE 1024
#define QUEUE_SIZE 20
#define HEALTH_PERIOD  100
#define PRODUCER_SPEED 100
#define CONSUMER_SPEED 5000
#define WDT_TIMEOUT (2 * QUEUE_SIZE * PRODUCER_SPEED)

struct sensor_data {
    uint32_t id;
    uint32_t val1;
    uint32_t val2;
};

K_MSGQ_DEFINE(sensor_queue, sizeof(struct sensor_data), QUEUE_SIZE, 4);

static void task_wdt_callback(int channel_id, void *user_data)
{
    ARG_UNUSED(user_data);

    LOG_INF("[WDT] Watchdog callback fires for channel %d. Consumer thread took too long.", channel_id);
}

static void sensor_consumer_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    task_wdt_init(NULL);
    int channel_id = task_wdt_add(WDT_TIMEOUT, task_wdt_callback, NULL);

    struct sensor_data data;

    while (true) {
        int ret = k_msgq_get(&sensor_queue, &data, K_FOREVER);
        if (ret != 0) {
            LOG_INF("[SUBSCRIBER] Error when waiting for message, %d", ret);
            break;
        }

        LOG_INF("[SUBSCRIBER] thread %s processing msg [%d](%d,%d)",
                k_thread_name_get(k_current_get()),
                data.id,
                data.val1,
                data.val2);
        
        // Simulate busy work that will fill the queue up
        k_sleep(K_MSEC(CONSUMER_SPEED));

        task_wdt_feed(channel_id);
    }
}

static void sensor_producer_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (int i = 0;; ++i) {
        struct sensor_data data = {
            i,
            .val1 = 120 + i * 3,
            .val2 = 210 + i * 2
        };

        LOG_INF("[SENSOR] Publish [%d](%d,%d)", data.id, data.val1, data.val2);

        k_msgq_put(&sensor_queue, &data, K_NO_WAIT);

        k_sleep(K_MSEC(PRODUCER_SPEED));
    }
}

static void sensor_health_check_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        int queue_fill = k_msgq_num_used_get(&sensor_queue);
        if (4 * queue_fill > 3 * QUEUE_SIZE) {
            LOG_WRN("Queue is becoming full - %d/%d", queue_fill, QUEUE_SIZE);
        }

        k_sleep(K_MSEC(HEALTH_PERIOD));
    }
}

K_THREAD_DEFINE(consumer_thread, STACK_SIZE, sensor_consumer_thread,
                NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(producer_thread, STACK_SIZE, sensor_producer_thread,
                NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(health_thread, STACK_SIZE, sensor_health_check_thread,
                NULL, NULL, NULL, PRIORITY + 1, 0, 0);
