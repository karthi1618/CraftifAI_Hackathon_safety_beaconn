#include "app.h"
#include "app_config.h"
#include "ble_dht22_adv.h"
#include "dht22_provider.h"
#include "logger.h"

static const char *TAG = "app";

void app_start(void)
{
    ESP_LOGI(TAG, "firmware started");

    // Start DHT sampling (GPIO2) and then BLE advertising.
    dht22_provider_start();
    ble_dht22_adv_start();
}
