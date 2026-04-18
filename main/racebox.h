#ifndef RACEBOX_H
#define RACEBOX_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#define RACEBOX_VISIBLE_DEVICES_MAX 16

typedef struct
{
  char name[32];
  char address[18];
  int16_t rssi;
} racebox_visible_device_t;

typedef void (*racebox_rx_callback_t)(const uint8_t* data, size_t len, void* user_ctx);

esp_err_t racebox_init(void);
size_t racebox_get_visible_devices(racebox_visible_device_t* out_devices, size_t max_devices);
esp_err_t racebox_connect_visible_device(size_t index);
esp_err_t racebox_request_scan(void);
esp_err_t racebox_disconnect(void);
void racebox_set_rx_callback(racebox_rx_callback_t cb, void* user_ctx);

#endif // RACEBOX_H