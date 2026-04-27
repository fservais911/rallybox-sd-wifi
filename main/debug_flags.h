#pragma once

#include "esp_log.h"

#ifdef GPIO_SD_DEBUG
#define GPIO_SD_DEBUG_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define GPIO_SD_DEBUG_LOGW(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#else
#define GPIO_SD_DEBUG_LOGI(tag, ...) do {} while (0)
#define GPIO_SD_DEBUG_LOGW(tag, ...) do {} while (0)
#endif

#ifdef IST_TIME_DEBUG
#define IST_TIME_DEBUG_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#else
#define IST_TIME_DEBUG_LOGI(tag, ...) do {} while (0)
#endif

#ifdef UI_STORAGE_DEBUG
#define UI_STORAGE_DEBUG_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define UI_STORAGE_DEBUG_LOGW(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#else
#define UI_STORAGE_DEBUG_LOGI(tag, ...) do {} while (0)
#define UI_STORAGE_DEBUG_LOGW(tag, ...) do {} while (0)
#endif

#ifdef TOUCH_DEBUG
#define TOUCH_DEBUG_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#else
#define TOUCH_DEBUG_LOGI(tag, ...) do {} while (0)
#endif