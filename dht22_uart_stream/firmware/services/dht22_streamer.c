#include "dht22_streamer.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "dht22.h"
#include "logger.h"

#include "esp_err.h"
#include "esp_timer.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "dht22_stream"

typedef struct {
    int64_t ts_us;
    float temp_c;
    float rh;
    int valid;
    int err;
    uint32_t seq;
} latest_sample_t;

static SemaphoreHandle_t s_lock;
static latest_sample_t s_latest;
static uint32_t s_dropped_lines;

static void uart0_init_once(void)
{
    static bool inited = false;
    if (inited) {
        return;
    }
    inited = true;

    uart_config_t cfg = {
        .baud_rate = APP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(APP_UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(APP_UART_NUM, &cfg));

    // UART0 pins are fixed to the console on this board (USB-serial bridge).
    // No uart_set_pin() needed.
}

static void sample_set(const latest_sample_t *s)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_latest = *s;
    xSemaphoreGive(s_lock);
}

static latest_sample_t sample_get(void)
{
    latest_sample_t s;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s = s_latest;
    xSemaphoreGive(s_lock);
    return s;
}

static void dht_poll_task(void *arg)
{
    (void)arg;

    const TickType_t delay_ticks = pdMS_TO_TICKS(APP_DHT22_POLL_INTERVAL_MS);

    // Initialize with invalid sample
    latest_sample_t s = {
        .ts_us = esp_timer_get_time(),
        .temp_c = 0,
        .rh = 0,
        .valid = 0,
        .err = DHT22_ERR_TIMEOUT,
        .seq = 0,
    };
    sample_set(&s);

    while (1) {
        int64_t now = esp_timer_get_time();

        dht22_reading_t r;
        dht22_err_t e = dht22_read(APP_DHT22_GPIO, &r, 5000);

        s.ts_us = now;
        s.seq++;
        if (e == DHT22_OK) {
            s.temp_c = r.temperature_c;
            s.rh = r.humidity_rh;
            s.valid = 1;
            s.err = 0;
        } else {
            s.valid = 0;
            s.err = (int)e;
        }
        sample_set(&s);

        vTaskDelay(delay_ticks);
    }
}

static void dht_stream_task(void *arg)
{
    (void)arg;

#if APP_PRINT_CSV_HEADER
    const char *hdr = "ts_us,temp_c,humidity_rh,valid,err,seq,age_ms\r\n";
    (void)uart_write_bytes(APP_UART_NUM, hdr, (size_t)strlen(hdr));
#endif

    const TickType_t delay_ticks = pdMS_TO_TICKS(APP_STREAM_PERIOD_MS);

    while (1) {
        const int64_t now = esp_timer_get_time();
        latest_sample_t s = sample_get();
        int64_t age_us = now - s.ts_us;
        if (age_us < 0) {
            age_us = 0;
        }

        char line[128];
        int n = snprintf(line, sizeof(line),
                         "%" PRId64 ",%.3f,%.3f,%d,%d,%" PRIu32 ",%" PRId64 "\r\n",
                         now,
                         (double)s.temp_c,
                         (double)s.rh,
                         s.valid,
                         s.err,
                         s.seq,
                         (int64_t)(age_us / 1000));

        if (n > 0) {
            int wr = uart_write_bytes(APP_UART_NUM, line, (size_t)n);
            if (wr < 0) {
                s_dropped_lines++;
            }
        } else {
            s_dropped_lines++;
        }

        vTaskDelay(delay_ticks);
    }
}

void dht22_streamer_start(void)
{
    uart0_init_once();

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }

    // Keep tasks pinned to no core (ESP32-C3 is single core)
    xTaskCreate(dht_poll_task, "dht_poll", 4096, NULL, 8, NULL);
    xTaskCreate(dht_stream_task, "dht_stream", 4096, NULL, 7, NULL);

    ESP_LOGI(TAG, "DHT22 streamer started (GPIO=%d poll=%dms stream=%dms baud=%d)",
             APP_DHT22_GPIO, APP_DHT22_POLL_INTERVAL_MS, APP_STREAM_PERIOD_MS, APP_UART_BAUD);
}

