#include "dht22.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// DHT22 protocol timing (microseconds)
#define DHT_START_LOW_US     1100
#define DHT_START_RELEASE_US 30

#define DHT_EXPECTED_PRE_HIGH_US_MAX  100
#define DHT_EXPECTED_PRE_LOW_US_MAX   120
#define DHT_BIT_HIGH_0_MAX_US         45
#define DHT_BIT_HIGH_1_MIN_US         55

static inline int IRAM_ATTR wait_level(gpio_num_t gpio, int level, uint32_t timeout_us, uint32_t *elapsed_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) == level) {
        if ((uint32_t)(esp_timer_get_time() - start) >= timeout_us) {
            if (elapsed_us) {
                *elapsed_us = timeout_us;
            }
            return -1;
        }
    }
    if (elapsed_us) {
        *elapsed_us = (uint32_t)(esp_timer_get_time() - start);
    }
    return 0;
}

static inline int IRAM_ATTR wait_level_eq(gpio_num_t gpio, int level, uint32_t timeout_us, uint32_t *elapsed_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) != level) {
        if ((uint32_t)(esp_timer_get_time() - start) >= timeout_us) {
            if (elapsed_us) {
                *elapsed_us = timeout_us;
            }
            return -1;
        }
    }
    if (elapsed_us) {
        *elapsed_us = (uint32_t)(esp_timer_get_time() - start);
    }
    return 0;
}

dht22_err_t dht22_read(int gpio_num, dht22_reading_t *out, uint32_t timeout_us)
{
    if (!out) {
        return DHT22_ERR_PROTOCOL;
    }

    const gpio_num_t gpio = (gpio_num_t)gpio_num;

    // Configure as open-drain-ish output (we'll switch direction)
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&cfg);

    // Start signal: pull low for >= 1ms
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(DHT_START_LOW_US);

    // Release and switch to input
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(DHT_START_RELEASE_US);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);

    uint8_t data[5] = {0};

    // Timing critical section
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    // Sensor response: ~80us low then ~80us high
    uint32_t t = 0;
    if (wait_level_eq(gpio, 0, timeout_us, &t) != 0) {
        portEXIT_CRITICAL(&mux);
        return DHT22_ERR_TIMEOUT;
    }
    if (wait_level(gpio, 0, timeout_us, &t) != 0 || t > DHT_EXPECTED_PRE_LOW_US_MAX) {
        portEXIT_CRITICAL(&mux);
        return DHT22_ERR_PROTOCOL;
    }
    if (wait_level(gpio, 1, timeout_us, &t) != 0 || t > DHT_EXPECTED_PRE_HIGH_US_MAX) {
        portEXIT_CRITICAL(&mux);
        return DHT22_ERR_PROTOCOL;
    }

    // Read 40 bits: each bit starts with ~50us low, then high for 26-28us (0) or ~70us (1)
    for (int i = 0; i < 40; i++) {
        // wait for low pulse start
        if (wait_level_eq(gpio, 0, timeout_us, NULL) != 0) {
            portEXIT_CRITICAL(&mux);
            return DHT22_ERR_TIMEOUT;
        }
        // measure low duration (should be ~50us)
        if (wait_level(gpio, 0, timeout_us, &t) != 0 || t > 120) {
            portEXIT_CRITICAL(&mux);
            return DHT22_ERR_PROTOCOL;
        }
        // measure high duration
        if (wait_level(gpio, 1, timeout_us, &t) != 0) {
            portEXIT_CRITICAL(&mux);
            return DHT22_ERR_TIMEOUT;
        }

        int bit = 0;
        if (t >= DHT_BIT_HIGH_1_MIN_US) {
            bit = 1;
        } else if (t <= DHT_BIT_HIGH_0_MAX_US) {
            bit = 0;
        } else {
            portEXIT_CRITICAL(&mux);
            return DHT22_ERR_PROTOCOL;
        }

        data[i / 8] <<= 1;
        data[i / 8] |= (uint8_t)bit;
    }

    portEXIT_CRITICAL(&mux);

    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) {
        return DHT22_ERR_CHECKSUM;
    }

    // DHT22: humidity*10 in first 16 bits, temperature*10 in next 16 (signed)
    uint16_t rh10 = (uint16_t)((data[0] << 8) | data[1]);
    int16_t t10 = (int16_t)((data[2] << 8) | data[3]);

    out->humidity_rh = (float)rh10 / 10.0f;
    out->temperature_c = (float)t10 / 10.0f;

    return DHT22_OK;
}
