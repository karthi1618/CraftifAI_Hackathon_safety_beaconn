#ifndef DHT22_STREAMER_H
#define DHT22_STREAMER_H

#ifdef __cplusplus
extern "C" {
#endif

// UART CSV streaming (optional feature; not used by BLE advertiser build)
void dht22_streamer_start(void);

#ifdef __cplusplus
}
#endif

#endif // DHT22_STREAMER_H
