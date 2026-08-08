#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// DHT22 configuration
#define APP_DHT22_GPIO                 2
#define APP_DHT22_MIN_POLL_INTERVAL_MS 2000
#define APP_DHT22_POLL_INTERVAL_MS     2000

// Streaming configuration
#define APP_STREAM_PERIOD_MS           20   // 50 Hz stream of latest sample
#define APP_PRINT_CSV_HEADER           1

// UART configuration (UART0 via USB-Serial on ESP32-C3 DevKitM-1)
#define APP_UART_NUM                   0
#define APP_UART_BAUD                  115200

#endif // APP_CONFIG_H
