#ifndef DHT22_PROVIDER_H
#define DHT22_PROVIDER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t ts_us;
    float temp_c;
    float humidity_rh;
    bool valid;
    uint8_t err;
    uint32_t seq;
} dht22_sample_t;

void dht22_provider_start(void);

/** Get latest sample (thread-safe copy). */
dht22_sample_t dht22_provider_get_latest(void);

#ifdef __cplusplus
}
#endif

#endif // DHT22_PROVIDER_H
