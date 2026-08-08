#include "dht22_provider.h"

#include <string.h>

#include "app_config.h"
#include "dht22.h"
#include "logger.h"

#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "dht22_provider"

typedef struct {
    SemaphoreHandle_t lock;
    dht22_sample_t latest;
    bool started;
} dht22_provider_state_t;

static dht22_provider_state_t s;

static void sample_set(const dht22_sample_t *in)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.latest = *in;
    xSemaphoreGive(s.lock);
}

dht22_sample_t dht22_provider_get_latest(void)
{
    dht22_sample_t out;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    out = s.latest;
    xSemaphoreGive(s.lock);
    return out;
}

static void dht_poll_task(void *arg)
{
    (void)arg;

    const TickType_t delay_ticks = pdMS_TO_TICKS(APP_DHT22_POLL_INTERVAL_MS);

    dht22_sample_t smp = {
        .ts_us = esp_timer_get_time(),
        .temp_c = 0,
        .humidity_rh = 0,
        .valid = false,
        .err = (uint8_t)DHT22_ERR_TIMEOUT,
        .seq = 0,
    };
    sample_set(&smp);

    while (1) {
        int64_t now = esp_timer_get_time();

        dht22_reading_t r;
        dht22_err_t e = dht22_read(APP_DHT22_GPIO, &r, 5000);

        smp.ts_us = now;
        smp.seq++;

        if (e == DHT22_OK) {
            smp.temp_c = r.temperature_c;
            smp.humidity_rh = r.humidity_rh;
            smp.valid = true;
            smp.err = 0;
        } else {
            smp.valid = false;
            smp.err = (uint8_t)e;
        }

        sample_set(&smp);
        vTaskDelay(delay_ticks);
    }
}

void dht22_provider_start(void)
{
    if (s.started) {
        return;
    }

    s.lock = xSemaphoreCreateMutex();
    if (!s.lock) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    s.started = true;
    xTaskCreate(dht_poll_task, "dht_poll", 4096, NULL, 8, NULL);

    ESP_LOGI(TAG, "DHT22 provider started (GPIO=%d poll=%dms)",
             APP_DHT22_GPIO, APP_DHT22_POLL_INTERVAL_MS);
}
