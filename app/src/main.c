#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(l4_task1, LOG_LEVEL_DBG);

#define PRIORITY 5
#define STACK_SIZE 1024
#define SUBSCRIBER_QUEUE_SIZE 3

struct sensor_data {
    uint32_t id;
    uint32_t val1;
    uint32_t val2;
};

static void sensor_listener_cb(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(sensor_listener, sensor_listener_cb);
ZBUS_SUBSCRIBER_DEFINE(sensor_subscriber, SUBSCRIBER_QUEUE_SIZE);

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data, NULL, NULL,
                 ZBUS_OBSERVERS(sensor_listener, sensor_subscriber),
                 ZBUS_MSG_INIT(.id = 0, .val1 = 0, .val2 = 0));

static void sensor_listener_cb(const struct zbus_channel *chan)
{
    const struct sensor_data *data = 
        (const struct sensor_data*)zbus_chan_const_msg(chan);

    LOG_INF("[LISTENER] thread %s processed msg [%d](%d,%d)",
            k_thread_name_get(k_current_get()),
            data->id,
            data->val1,
            data->val2);
}

static void sensor_subscriber_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct zbus_channel *chan;

    while (true) {
        int ret = zbus_sub_wait(&sensor_subscriber, &chan, K_FOREVER);
        if (ret != 0) {
            LOG_INF("[SUBSCRIBER] Error when waiting for channel, %d", ret);
            break;
        }

        struct sensor_data data;

        ret = zbus_chan_read(chan, &data, K_FOREVER);
        if (ret != 0) {
            LOG_WRN("[SUBSCRIBER] Read failed with error %d", ret);
            continue;
        }

        LOG_INF("[SUBSCRIBER] thread %s processed msg [%d](%d,%d)",
                k_thread_name_get(k_current_get()),
                data.id,
                data.val1,
                data.val2);
        
        // Simulate busy work. Queue will overflow with too many
        // messages produced and subscriber will be able to only
        // process some of them, others will be lost.
        k_sleep(K_MSEC(500));
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

        zbus_chan_pub(&sensor_chan, &data, K_NO_WAIT);

        k_sleep(K_MSEC(100));
    }
}

K_THREAD_DEFINE(subscriber_thread, STACK_SIZE, sensor_subscriber_thread,
                NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(producer_thread, STACK_SIZE, sensor_producer_thread,
                NULL, NULL, NULL, PRIORITY, 0, 0);
