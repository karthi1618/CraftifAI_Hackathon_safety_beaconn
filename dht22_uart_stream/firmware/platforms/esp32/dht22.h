#ifndef DHT22_H
#define DHT22_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DHT22_OK = 0,
    DHT22_ERR_TIMEOUT,
    DHT22_ERR_CHECKSUM,
    DHT22_ERR_PROTOCOL,
} dht22_err_t;

typedef struct {
    float temperature_c;
    float humidity_rh;
} dht22_reading_t;

/**
 * @brief Read DHT22 sensor.
 *
 * @param gpio_num     GPIO number for the DHT data pin.
 * @param out          Output reading (valid only if return == DHT22_OK)
 * @param timeout_us   Overall timeout waiting for edges (typical 2000-5000 us)
 * @return dht22_err_t
 */
dht22_err_t dht22_read(int gpio_num, dht22_reading_t *out, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif // DHT22_H
