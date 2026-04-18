#ifndef GNSS_H
#define GNSS_H

#include "esp_err.h"
#include <stdbool.h>

typedef void (*gnss_sentence_callback_t)(const char* sentence, void* user_ctx);

esp_err_t gnss_init(void);
esp_err_t gnss_start_with_config(int baud_rate, int tx_gpio, int rx_gpio, bool persist);
esp_err_t gnss_stop(void);
bool gnss_is_running(void);
esp_err_t gnss_get_config(int* baud_rate, int* tx_gpio, int* rx_gpio);
void gnss_set_sentence_callback(gnss_sentence_callback_t callback, void* user_ctx);

#endif // GNSS_H