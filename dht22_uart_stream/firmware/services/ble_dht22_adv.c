#include "ble_dht22_adv.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "dht22_provider.h"
#include "logger.h"

#include "esp_err.h"
#include "esp_timer.h"

#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "os/os_mbuf.h"

#define TAG "ble_dht22_adv"

#define ADV_INSTANCE 0

// Advertising timing (units of 0.625ms). 200ms => 320 units.
#define ADV_ITVL_MS 200
#define ADV_ITVL_UNITS ((uint16_t)(ADV_ITVL_MS * 1000 / 625))

// Safe thresholds (defaults). Can be moved to Kconfig later.
#define SAFE_TEMP_MIN_C   0.0f
#define SAFE_TEMP_MAX_C  50.0f
#define SAFE_RH_MIN_PCT  20.0f
#define SAFE_RH_MAX_PCT  80.0f
#define SAFE_MAX_AGE_MS  5000

static uint8_t s_addr_type;
static bool s_started;

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool sample_is_safe(const dht22_sample_t *s, uint32_t age_ms)
{
    if (!s->valid) {
        return false;
    }
    if (age_ms > SAFE_MAX_AGE_MS) {
        return false;
    }
    if (s->temp_c < SAFE_TEMP_MIN_C || s->temp_c > SAFE_TEMP_MAX_C) {
        return false;
    }
    if (s->humidity_rh < SAFE_RH_MIN_PCT || s->humidity_rh > SAFE_RH_MAX_PCT) {
        return false;
    }
    return true;
}

static int build_mfg_payload(uint8_t *out, size_t out_len)
{
    // Manufacturer specific payload (binary, little-endian):
    // [0..1] company_id (0xFFFF demo)
    // [2]    version (0x01)
    // [3]    safe flag
    // [4..5] temp_x10 (int16)
    // [6..7] rh_x10 (uint16)
    // [8..9] age_ms (uint16)
    // [10]   err code
    if (out_len < 11) {
        return -1;
    }

    const int64_t now = esp_timer_get_time();
    dht22_sample_t s = dht22_provider_get_latest();
    int64_t age_us = now - s.ts_us;
    if (age_us < 0) {
        age_us = 0;
    }
    uint32_t age_ms32 = (uint32_t)(age_us / 1000);
    uint16_t age_ms = (age_ms32 > 0xFFFFu) ? 0xFFFFu : (uint16_t)age_ms32;

    const bool safe = sample_is_safe(&s, age_ms32);

    int16_t temp_x10 = (int16_t)((s.temp_c >= 0) ? (s.temp_c * 10.0f + 0.5f) : (s.temp_c * 10.0f - 0.5f));
    uint16_t rh_x10 = (uint16_t)((s.humidity_rh * 10.0f) + 0.5f);

    out[0] = 0xFF;
    out[1] = 0xFF;
    out[2] = 0x01;
    out[3] = safe ? 1 : 0;
    out[4] = (uint8_t)(temp_x10 & 0xFF);
    out[5] = (uint8_t)((temp_x10 >> 8) & 0xFF);
    out[6] = (uint8_t)(rh_x10 & 0xFF);
    out[7] = (uint8_t)((rh_x10 >> 8) & 0xFF);
    out[8] = (uint8_t)(age_ms & 0xFF);
    out[9] = (uint8_t)((age_ms >> 8) & 0xFF);
    out[10] = s.err;

    return 11;
}

static int set_adv_data_ext(void)
{
#if MYNEWT_VAL(BLE_EXT_ADV)
    // Build full advertising payload (AD structures):
    // Flags + Complete Name + Manufacturer Specific Data
    uint8_t mfg[16];
    int mfg_len = build_mfg_payload(mfg, sizeof(mfg));
    if (mfg_len < 0) {
        return BLE_HS_EINVAL;
    }

    const char *name = "DHT22";
    const size_t name_len = strlen(name);

    // AD structures: [len][type][data...]
    uint8_t adv[64];
    size_t idx = 0;

    // Flags
    adv[idx++] = 2;
    adv[idx++] = 0x01;
    adv[idx++] = 0x06; // general discoverable + BR/EDR unsupported

    // Complete local name
    if (idx + 2 + name_len <= sizeof(adv)) {
        adv[idx++] = (uint8_t)(1 + name_len);
        adv[idx++] = 0x09;
        memcpy(&adv[idx], name, name_len);
        idx += name_len;
    }

    // Manufacturer specific
    if (idx + 2 + (size_t)mfg_len <= sizeof(adv)) {
        adv[idx++] = (uint8_t)(1 + (size_t)mfg_len);
        adv[idx++] = 0xFF;
        memcpy(&adv[idx], mfg, (size_t)mfg_len);
        idx += (size_t)mfg_len;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(adv, (uint16_t)idx);
    if (!om) {
        return BLE_HS_ENOMEM;
    }
    return ble_gap_ext_adv_set_data(ADV_INSTANCE, om);
#else
    return BLE_HS_ENOTSUP;
#endif
}

static void advertise_start(void)
{
    int rc;

#if MYNEWT_VAL(BLE_EXT_ADV)
    struct ble_gap_ext_adv_params params;
    memset(&params, 0, sizeof(params));

    params.connectable = 0;
    params.scannable = 0;
    params.legacy_pdu = 0; // extended
    params.own_addr_type = s_addr_type;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.sid = 0;

    // Use fixed interval in units of 0.625 ms
    params.itvl_min = ADV_ITVL_UNITS;
    params.itvl_max = ADV_ITVL_UNITS;

    int8_t selected_tx_power = 0;
    rc = ble_gap_ext_adv_configure(ADV_INSTANCE, &params, &selected_tx_power, NULL, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ext_adv_configure failed rc=%d; falling back to legacy adv", rc);
        goto legacy_fallback;
    }

    rc = set_adv_data_ext();
    if (rc != 0) {
        ESP_LOGW(TAG, "ext_adv_set_data failed rc=%d; falling back to legacy adv", rc);
        goto legacy_fallback;
    }

    rc = ble_gap_ext_adv_start(ADV_INSTANCE, 0, 0);
    if (rc != 0) {
        ESP_LOGW(TAG, "ext_adv_start failed rc=%d; falling back to legacy adv", rc);
        goto legacy_fallback;
    }

    ESP_LOGI(TAG, "Extended advertising started (interval=%dms)", ADV_ITVL_MS);
    return;
#endif

legacy_fallback:;
    // Legacy, non-connectable
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char *name = "DHT22";
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    uint8_t mfg[16];
    int mfg_len = build_mfg_payload(mfg, sizeof(mfg));
    if (mfg_len > 0) {
        fields.mfg_data = mfg;
        fields.mfg_data_len = (uint8_t)mfg_len;
    }

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params advp;
    memset(&advp, 0, sizeof(advp));
    advp.conn_mode = BLE_GAP_CONN_MODE_NON;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    advp.itvl_min = ADV_ITVL_UNITS;
    advp.itvl_max = ADV_ITVL_UNITS;

    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &advp, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Legacy advertising started (interval=%dms)", ADV_ITVL_MS);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        return;
    }

    ble_svc_gap_device_name_set("DHT22");

    advertise_start();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset reason=%d", reason);
}

static void adv_update_task(void *arg)
{
    (void)arg;
    // Update advertising data after each DHT poll. Since DHT poll is 2s, update at 2s as well.
    // Also gives a periodic refresh for scanners.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(APP_DHT22_POLL_INTERVAL_MS));
#if MYNEWT_VAL(BLE_EXT_ADV)
        int rc = set_adv_data_ext();
        if (rc != 0) {
            ESP_LOGW(TAG, "adv data update failed rc=%d", rc);
        }
#else
        (void)set_adv_data_ext;
#endif
    }
}

void ble_dht22_adv_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;

    // NVS required for BT PHY calibration data
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    nimble_port_freertos_init(ble_host_task);

    xTaskCreate(adv_update_task, "adv_upd", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "BLE DHT22 advertiser starting");
}
