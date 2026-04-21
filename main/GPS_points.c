#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>

#include "GPS_points.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if __has_include("esp_crt_bundle.h")
#include "esp_crt_bundle.h"
#define RALLYBOX_HAS_CRT_BUNDLE 1
#else
#define RALLYBOX_HAS_CRT_BUNDLE 0
#endif

#define TRACK_MIN_POINT_INTERVAL_MS CONFIG_RALLYBOX_TRACK_MIN_POINT_INTERVAL_MS
#define TRACK_PSRAM_BUDGET_DIVISOR 4U
#define GPS_POINTS_SETTINGS_NAMESPACE "gps_points"
#define GPS_POINTS_NVS_KEY_FILTER_STATIONARY "flt_stationary"
#define GPS_POINTS_NVS_KEY_FILTER_IMPOSSIBLE "flt_impossible"
#define GPS_POINTS_NVS_KEY_STATIONARY_RADIUS_M "stationary_m"
#define GPS_POINTS_DEFAULT_FILTER_STATIONARY false
#define GPS_POINTS_DEFAULT_FILTER_IMPOSSIBLE true
#define GPS_POINTS_DEFAULT_STATIONARY_RADIUS_M 3U
#define GPS_POINTS_MIN_STATIONARY_RADIUS_M 1U
#define GPS_POINTS_MAX_STATIONARY_RADIUS_M 20U
#define GPS_POINTS_STATIONARY_ELEVATION_M 3.0f
#define GPS_POINTS_MIN_VALID_UTC 946684800LL
#define GPS_POINTS_MAX_VALID_UTC 4102444800LL
#define GPS_POINTS_MIN_ELEVATION_M (-1000.0f)
#define GPS_POINTS_MAX_ELEVATION_M 20000.0f
#define GPX_WEB_URL_MAX_LEN 3072U
#define GPX_WEB_SIGNER_RESPONSE_MAX_LEN 4096U
#define GPX_WEB_HOST_MAX_LEN 256U
#define GPX_WEB_URI_MAX_LEN 512U
#define GPX_WEB_QUERY_MAX_LEN 3072U
#define GPX_WEB_REQUEST_PATH_MAX_LEN 3584U
#define GPX_WEB_HTTP_RX_BUFFER_LEN 512U
#define GPX_WEB_HTTP_TX_BUFFER_LEN 4096U

#ifndef CONFIG_RALLYBOX_GPX_S3_BUCKET
#define CONFIG_RALLYBOX_GPX_S3_BUCKET ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_S3_REGION
#define CONFIG_RALLYBOX_GPX_S3_REGION "us-east-1"
#endif
#ifndef CONFIG_RALLYBOX_GPX_S3_OBJECT_PREFIX
#define CONFIG_RALLYBOX_GPX_S3_OBJECT_PREFIX "tracks/"
#endif
#ifndef CONFIG_RALLYBOX_GPX_S3_ACCESS_KEY_ID
#define CONFIG_RALLYBOX_GPX_S3_ACCESS_KEY_ID ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_S3_SECRET_ACCESS_KEY
#define CONFIG_RALLYBOX_GPX_S3_SECRET_ACCESS_KEY ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_S3_SESSION_TOKEN
#define CONFIG_RALLYBOX_GPX_S3_SESSION_TOKEN ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_S3_PRESIGNED_QUERY
#define CONFIG_RALLYBOX_GPX_S3_PRESIGNED_QUERY ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_WEB_AUTH_HEADER
#define CONFIG_RALLYBOX_GPX_WEB_AUTH_HEADER ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_WEB_AUTH_VALUE
#define CONFIG_RALLYBOX_GPX_WEB_AUTH_VALUE ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_WEB_SIGNER_URL
#define CONFIG_RALLYBOX_GPX_WEB_SIGNER_URL ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_HEADER
#define CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_HEADER ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_VALUE
#define CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_VALUE ""
#endif
#ifndef CONFIG_RALLYBOX_GPX_S3_VIRTUAL_HOSTED_STYLE
#define CONFIG_RALLYBOX_GPX_S3_VIRTUAL_HOSTED_STYLE 1
#endif
#ifndef CONFIG_RALLYBOX_GPX_S3_USE_HTTPS
#define CONFIG_RALLYBOX_GPX_S3_USE_HTTPS 1
#endif

static const char* TAG = "GPS_points";

typedef struct
{
  SemaphoreHandle_t mutex;
  gps_point_t* points;
  size_t max_points;
  size_t head;
  size_t count;
  uint64_t last_store_us;
} track_buffer_t;

typedef struct
{
  SemaphoreHandle_t mutex;
  bool initialized;
  size_t free_psram_bytes;
  size_t total_budget_bytes;
  bool feed_active[GPS_POINTS_FEED_COUNT];
} track_capacity_t;

typedef struct
{
  SemaphoreHandle_t mutex;
  bool loaded;
  gps_points_filter_config_t config;
} gps_points_settings_t;

static track_buffer_t s_track_buffers[GPS_POINTS_FEED_COUNT] = { 0 };
static track_capacity_t s_track_capacity = { 0 };
static gps_points_settings_t s_gps_points_settings = { 0 };

static bool track_capacity_init(void);
static bool track_buffer_ensure_mutex(track_buffer_t* tb);
static bool gps_points_settings_ensure_loaded(void);
static bool gps_points_settings_save_locked(void);
static bool gps_points_value_is_impossible(double lat_deg, double lon_deg, float ele_m, time_t utc_time);
static bool gps_points_is_stationary_locked(const track_buffer_t* tb, double lat_deg, double lon_deg, float ele_m, uint8_t stationary_radius_m);
static double gps_points_distance_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);
static size_t track_capacity_active_count_locked(void);
static size_t track_capacity_points_for_active_count_locked(size_t active_count);
static bool track_buffer_resize_locked(track_buffer_t* tb, size_t new_max_points);
static bool track_capacity_rebalance_locked(void);
static bool track_buffer_init(gps_points_feed_t feed);
static bool track_buffer_copy_snapshot(gps_points_feed_t feed, gps_point_t** out_points, size_t* out_count, time_t* out_first_utc);
static bool track_expand_filename_template(const char* tmpl, const char* filename, char* out_url, size_t out_len);
static bool track_copy_trimmed_http_url(const char* src, char* out_url, size_t out_len);
static bool track_extract_json_url_value(const char* json, const char* key, char* out_url, size_t out_len);
static bool track_extract_signer_url_from_body(const char* body, char* out_url, size_t out_len);
static esp_err_t track_fetch_presigned_upload_url(const char* filename, char* out_url, size_t out_len);
static esp_err_t track_resolve_web_upload_url(const char* filename, char* out_url, size_t out_len);
static bool track_build_web_url(const char* filename, char* out_url, size_t out_len);
static esp_err_t track_upload_gpx_snapshot_to_url(gps_points_feed_t feed, const char* url, size_t* out_points, time_t* out_first_utc);
static void track_try_normalize_s3_upload_url(char* url, size_t url_len);
static bool sigv4_parse_url_parts(const char* url, char* out_host, size_t out_host_len, char* out_uri, size_t out_uri_len, char* out_query, size_t out_query_len);

static bool track_capacity_init(void)
{
  if (s_track_capacity.mutex == NULL)
  {
    s_track_capacity.mutex = xSemaphoreCreateMutex();
  }
  if (s_track_capacity.mutex == NULL)
  {
    return false;
  }

  if (xSemaphoreTake(s_track_capacity.mutex, pdMS_TO_TICKS(20)) != pdTRUE)
  {
    return false;
  }

  if (!s_track_capacity.initialized)
  {
    s_track_capacity.free_psram_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_track_capacity.total_budget_bytes = s_track_capacity.free_psram_bytes / TRACK_PSRAM_BUDGET_DIVISOR;
    s_track_capacity.initialized = true;

    if ((s_track_capacity.total_budget_bytes / sizeof(gps_point_t)) == 0)
    {
      ESP_LOGW(TAG,
        "Track logging disabled: free_psram=%u bytes, shared_budget=%u bytes, point_size=%u bytes",
        (unsigned)s_track_capacity.free_psram_bytes,
        (unsigned)s_track_capacity.total_budget_bytes,
        (unsigned)sizeof(gps_point_t));
    }
    else
    {
      ESP_LOGI(TAG,
        "Track capacity derived from PSRAM: free=%u bytes, shared_budget=%u bytes, point_size=%u bytes",
        (unsigned)s_track_capacity.free_psram_bytes,
        (unsigned)s_track_capacity.total_budget_bytes,
        (unsigned)sizeof(gps_point_t));
    }
  }

  xSemaphoreGive(s_track_capacity.mutex);
  return true;
}

static bool track_buffer_ensure_mutex(track_buffer_t* tb)
{
  if (tb == NULL)
  {
    return false;
  }

  if (tb->mutex == NULL)
  {
    tb->mutex = xSemaphoreCreateMutex();
  }

  return tb->mutex != NULL;
}

static bool gps_points_settings_ensure_loaded(void)
{
  nvs_handle_t handle;
  int32_t value = 0;

  if (s_gps_points_settings.mutex == NULL)
  {
    s_gps_points_settings.mutex = xSemaphoreCreateMutex();
  }
  if (s_gps_points_settings.mutex == NULL)
  {
    return false;
  }

  if (xSemaphoreTake(s_gps_points_settings.mutex, pdMS_TO_TICKS(50)) != pdTRUE)
  {
    return false;
  }

  if (!s_gps_points_settings.loaded)
  {
    s_gps_points_settings.config.filter_stationary_points = GPS_POINTS_DEFAULT_FILTER_STATIONARY;
    s_gps_points_settings.config.filter_impossible_values = GPS_POINTS_DEFAULT_FILTER_IMPOSSIBLE;
    s_gps_points_settings.config.stationary_radius_m = GPS_POINTS_DEFAULT_STATIONARY_RADIUS_M;

    if (nvs_open(GPS_POINTS_SETTINGS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK)
    {
      if (nvs_get_i32(handle, GPS_POINTS_NVS_KEY_FILTER_STATIONARY, &value) == ESP_OK)
      {
        s_gps_points_settings.config.filter_stationary_points = (value != 0);
      }
      if (nvs_get_i32(handle, GPS_POINTS_NVS_KEY_FILTER_IMPOSSIBLE, &value) == ESP_OK)
      {
        s_gps_points_settings.config.filter_impossible_values = (value != 0);
      }
      if (nvs_get_i32(handle, GPS_POINTS_NVS_KEY_STATIONARY_RADIUS_M, &value) == ESP_OK)
      {
        if (value < GPS_POINTS_MIN_STATIONARY_RADIUS_M)
        {
          value = GPS_POINTS_MIN_STATIONARY_RADIUS_M;
        }
        else if (value > GPS_POINTS_MAX_STATIONARY_RADIUS_M)
        {
          value = GPS_POINTS_MAX_STATIONARY_RADIUS_M;
        }
        s_gps_points_settings.config.stationary_radius_m = (uint8_t)value;
      }
      nvs_close(handle);
    }

    s_gps_points_settings.loaded = true;
    ESP_LOGI(TAG,
      "GPS point filters loaded: stationary=%d impossible=%d",
      s_gps_points_settings.config.filter_stationary_points ? 1 : 0,
      s_gps_points_settings.config.filter_impossible_values ? 1 : 0);
  }

  xSemaphoreGive(s_gps_points_settings.mutex);
  return true;
}

static bool gps_points_settings_save_locked(void)
{
  nvs_handle_t handle;
  esp_err_t err;

  err = nvs_open(GPS_POINTS_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK)
  {
    return false;
  }

  err = nvs_set_i32(handle,
    GPS_POINTS_NVS_KEY_FILTER_STATIONARY,
    s_gps_points_settings.config.filter_stationary_points ? 1 : 0);
  if (err == ESP_OK)
  {
    err = nvs_set_i32(handle,
      GPS_POINTS_NVS_KEY_FILTER_IMPOSSIBLE,
      s_gps_points_settings.config.filter_impossible_values ? 1 : 0);
  }
  if (err == ESP_OK)
  {
    err = nvs_set_i32(handle,
      GPS_POINTS_NVS_KEY_STATIONARY_RADIUS_M,
      (int32_t)s_gps_points_settings.config.stationary_radius_m);
  }
  if (err == ESP_OK)
  {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err == ESP_OK;
}

static bool gps_points_value_is_impossible(double lat_deg, double lon_deg, float ele_m, time_t utc_time)
{
  if (!isfinite(lat_deg) || !isfinite(lon_deg) || !isfinite((double)ele_m))
  {
    return true;
  }

  if (lat_deg < -90.0 || lat_deg > 90.0)
  {
    return true;
  }
  if (lon_deg < -180.0 || lon_deg > 180.0)
  {
    return true;
  }
  if (ele_m < GPS_POINTS_MIN_ELEVATION_M || ele_m > GPS_POINTS_MAX_ELEVATION_M)
  {
    return true;
  }
  if (((int64_t)utc_time) < GPS_POINTS_MIN_VALID_UTC || ((int64_t)utc_time) > GPS_POINTS_MAX_VALID_UTC)
  {
    return true;
  }

  return false;
}

static double gps_points_distance_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg)
{
  const double earth_radius_m = 6371000.0;
  const double deg_to_rad = 0.017453292519943295;
  double lat1_rad = lat1_deg * deg_to_rad;
  double lat2_rad = lat2_deg * deg_to_rad;
  double delta_lat = (lat2_deg - lat1_deg) * deg_to_rad;
  double delta_lon = (lon2_deg - lon1_deg) * deg_to_rad;
  double sin_lat = sin(delta_lat * 0.5);
  double sin_lon = sin(delta_lon * 0.5);
  double hav = (sin_lat * sin_lat) + (cos(lat1_rad) * cos(lat2_rad) * sin_lon * sin_lon);
  double clamped_hav = hav > 1.0 ? 1.0 : hav;
  double arc = 2.0 * atan2(sqrt(clamped_hav), sqrt(1.0 - clamped_hav));

  return earth_radius_m * arc;
}

static bool gps_points_is_stationary_locked(const track_buffer_t* tb, double lat_deg, double lon_deg, float ele_m, uint8_t stationary_radius_m)
{
  size_t last_idx;
  const gps_point_t* last_point;
  double horizontal_distance_m;

  if (tb == NULL || tb->count == 0 || tb->points == NULL || tb->max_points == 0)
  {
    return false;
  }

  last_idx = (tb->head + tb->count - 1) % tb->max_points;
  last_point = &tb->points[last_idx];
  horizontal_distance_m = gps_points_distance_m(last_point->lat_deg, last_point->lon_deg, lat_deg, lon_deg);

  if (horizontal_distance_m >= (double)stationary_radius_m)
  {
    return false;
  }

  return fabsf(ele_m - last_point->ele_m) < GPS_POINTS_STATIONARY_ELEVATION_M;
}

static size_t track_capacity_active_count_locked(void)
{
  size_t active_count = 0;
  size_t feed;

  for (feed = 0; feed < GPS_POINTS_FEED_COUNT; ++feed)
  {
    if (s_track_capacity.feed_active[feed])
    {
      active_count++;
    }
  }

  return active_count;
}

static size_t track_capacity_points_for_active_count_locked(size_t active_count)
{
  size_t bytes_per_feed;

  if (active_count == 0)
  {
    return 0;
  }

  bytes_per_feed = s_track_capacity.total_budget_bytes / active_count;
  return bytes_per_feed / sizeof(gps_point_t);
}

static bool track_buffer_resize_locked(track_buffer_t* tb, size_t new_max_points)
{
  gps_point_t* new_points = NULL;
  size_t keep_count = 0;
  size_t i;

  if (tb == NULL)
  {
    return false;
  }

  if (!track_buffer_ensure_mutex(tb))
  {
    return false;
  }

  if (xSemaphoreTake(tb->mutex, pdMS_TO_TICKS(50)) != pdTRUE)
  {
    return false;
  }

  if (tb->max_points == new_max_points)
  {
    xSemaphoreGive(tb->mutex);
    return true;
  }

  if (new_max_points > 0)
  {
    size_t bytes = new_max_points * sizeof(gps_point_t);
    new_points = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new_points == NULL)
    {
      xSemaphoreGive(tb->mutex);
      return false;
    }

    keep_count = tb->count;
    if (keep_count > new_max_points)
    {
      keep_count = new_max_points;
    }

    if (tb->points != NULL && tb->max_points > 0 && keep_count > 0)
    {
      size_t start = (tb->head + (tb->count - keep_count)) % tb->max_points;
      for (i = 0; i < keep_count; ++i)
      {
        new_points[i] = tb->points[(start + i) % tb->max_points];
      }
    }
  }

  if (tb->points != NULL)
  {
    free(tb->points);
  }

  tb->points = new_points;
  tb->max_points = new_max_points;
  tb->head = 0;
  tb->count = keep_count;
  if (new_max_points == 0)
  {
    tb->last_store_us = 0;
  }

  xSemaphoreGive(tb->mutex);
  return true;
}

static bool track_capacity_rebalance_locked(void)
{
  size_t active_count = track_capacity_active_count_locked();
  size_t target_points = track_capacity_points_for_active_count_locked(active_count);
  size_t feed;

  for (feed = 0; feed < GPS_POINTS_FEED_COUNT; ++feed)
  {
    track_buffer_t* tb = &s_track_buffers[feed];
    size_t target = s_track_capacity.feed_active[feed] ? target_points : 0;

    if (!track_buffer_ensure_mutex(tb))
    {
      return false;
    }

    if (tb->max_points > target)
    {
      if (!track_buffer_resize_locked(tb, target))
      {
        return false;
      }
    }
  }

  for (feed = 0; feed < GPS_POINTS_FEED_COUNT; ++feed)
  {
    track_buffer_t* tb = &s_track_buffers[feed];
    size_t target = s_track_capacity.feed_active[feed] ? target_points : 0;

    if (tb->max_points < target)
    {
      if (!track_buffer_resize_locked(tb, target))
      {
        return false;
      }
    }
  }

  ESP_LOGI(TAG,
    "Track rebalance: active_feeds=%u target_points_per_active_feed=%u",
    (unsigned)active_count,
    (unsigned)target_points);
  return true;
}

void gps_points_set_feed_active(gps_points_feed_t feed, bool active)
{
  bool previous_active;

  if (feed >= GPS_POINTS_FEED_COUNT)
  {
    return;
  }

  if (!track_capacity_init())
  {
    return;
  }

  if (xSemaphoreTake(s_track_capacity.mutex, pdMS_TO_TICKS(50)) != pdTRUE)
  {
    return;
  }

  previous_active = s_track_capacity.feed_active[feed];
  if (previous_active != active)
  {
    s_track_capacity.feed_active[feed] = active;
    if (!track_capacity_rebalance_locked())
    {
      s_track_capacity.feed_active[feed] = previous_active;
      (void)track_capacity_rebalance_locked();
      ESP_LOGE(TAG, "Failed to rebalance track buffers for feed %d active=%d", (int)feed, active ? 1 : 0);
    }
  }

  xSemaphoreGive(s_track_capacity.mutex);
}

bool gps_points_is_feed_active(gps_points_feed_t feed)
{
  bool active = false;

  if (feed >= GPS_POINTS_FEED_COUNT)
  {
    return false;
  }

  if (!track_capacity_init())
  {
    return false;
  }

  if (xSemaphoreTake(s_track_capacity.mutex, pdMS_TO_TICKS(20)) == pdTRUE)
  {
    active = s_track_capacity.feed_active[feed];
    xSemaphoreGive(s_track_capacity.mutex);
  }

  return active;
}

esp_err_t gps_points_reset_feed(gps_points_feed_t feed)
{
  track_buffer_t* tb;

  if (feed >= GPS_POINTS_FEED_COUNT)
  {
    return ESP_ERR_INVALID_ARG;
  }

  tb = &s_track_buffers[feed];
  if (!track_buffer_ensure_mutex(tb))
  {
    return ESP_ERR_NO_MEM;
  }

  if (xSemaphoreTake(tb->mutex, pdMS_TO_TICKS(20)) != pdTRUE)
  {
    return ESP_ERR_TIMEOUT;
  }

  tb->head = 0;
  tb->count = 0;
  tb->last_store_us = 0;

  xSemaphoreGive(tb->mutex);
  return ESP_OK;
}

bool gps_points_get_usage(gps_points_feed_t feed, size_t* out_points, size_t* out_capacity_points, uint8_t* out_fill_percent)
{
  size_t points = 0;
  size_t capacity_points = 0;
  uint8_t fill_percent = 0;

  if (out_points == NULL || out_capacity_points == NULL || out_fill_percent == NULL)
  {
    return false;
  }

  *out_points = 0;
  *out_capacity_points = 0;
  *out_fill_percent = 0;

  if (feed >= GPS_POINTS_FEED_COUNT)
  {
    return false;
  }

  if (gps_points_is_feed_active(feed))
  {
    capacity_points = gps_points_get_configured_max_points();
    if (capacity_points > 0)
    {
      time_t first_utc = 0;
      if (gps_points_get_summary(feed, &points, &first_utc) && points > 0)
      {
        if (points > capacity_points)
        {
          points = capacity_points;
        }
        fill_percent = (uint8_t)((points * 100U) / capacity_points);
      }
    }
  }

  *out_points = points;
  *out_capacity_points = capacity_points;
  *out_fill_percent = fill_percent;
  return true;
}

bool gps_points_get_filter_config(gps_points_filter_config_t* out_config)
{
  if (out_config == NULL)
  {
    return false;
  }

  if (!gps_points_settings_ensure_loaded())
  {
    return false;
  }

  if (xSemaphoreTake(s_gps_points_settings.mutex, pdMS_TO_TICKS(20)) != pdTRUE)
  {
    return false;
  }

  *out_config = s_gps_points_settings.config;
  xSemaphoreGive(s_gps_points_settings.mutex);
  return true;
}

esp_err_t gps_points_set_filter_config(const gps_points_filter_config_t* config)
{
  esp_err_t status = ESP_OK;
  gps_points_filter_config_t sanitized;

  if (config == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (!gps_points_settings_ensure_loaded())
  {
    return ESP_FAIL;
  }

  if (xSemaphoreTake(s_gps_points_settings.mutex, pdMS_TO_TICKS(50)) != pdTRUE)
  {
    return ESP_ERR_TIMEOUT;
  }

  sanitized = *config;
  if (sanitized.stationary_radius_m < GPS_POINTS_MIN_STATIONARY_RADIUS_M)
  {
    sanitized.stationary_radius_m = GPS_POINTS_MIN_STATIONARY_RADIUS_M;
  }
  else if (sanitized.stationary_radius_m > GPS_POINTS_MAX_STATIONARY_RADIUS_M)
  {
    sanitized.stationary_radius_m = GPS_POINTS_MAX_STATIONARY_RADIUS_M;
  }

  s_gps_points_settings.config = sanitized;
  if (!gps_points_settings_save_locked())
  {
    status = ESP_FAIL;
  }
  else
  {
    ESP_LOGI(TAG,
      "GPS point filters saved: stationary=%d impossible=%d",
      s_gps_points_settings.config.filter_stationary_points ? 1 : 0,
      s_gps_points_settings.config.filter_impossible_values ? 1 : 0);
  }

  xSemaphoreGive(s_gps_points_settings.mutex);
  return status;
}

size_t gps_points_get_configured_max_points(void)
{
  size_t active_count;

  if (!track_capacity_init())
  {
    return 0;
  }

  if (xSemaphoreTake(s_track_capacity.mutex, pdMS_TO_TICKS(20)) != pdTRUE)
  {
    return 0;
  }

  active_count = track_capacity_active_count_locked();
  xSemaphoreGive(s_track_capacity.mutex);
  return track_capacity_points_for_active_count_locked(active_count == 0 ? 1 : active_count);
}

size_t gps_points_get_point_size_bytes(void)
{
  return sizeof(gps_point_t);
}

size_t gps_points_get_configured_bytes_per_feed(void)
{
  return gps_points_get_configured_max_points() * sizeof(gps_point_t);
}

size_t gps_points_get_configured_bytes_total(void)
{
  return GPS_POINTS_FEED_COUNT * gps_points_get_configured_bytes_per_feed();
}

void gps_points_make_filename(gps_points_feed_t feed, time_t first_utc, char* out_name, size_t out_len)
{
  struct tm tm_utc = { 0 };
  const char* suffix = (feed == GPS_POINTS_FEED_BLE) ? "ble" : "gnss";

  if (out_name == NULL || out_len == 0)
  {
    return;
  }

  if (first_utc <= 0)
  {
    first_utc = time(NULL);
  }

  gmtime_r(&first_utc, &tm_utc);
  snprintf(out_name,
    out_len,
    "%02d%02d%04d-%02d%02d%02d.%s.gpx",
    tm_utc.tm_mday,
    tm_utc.tm_mon + 1,
    tm_utc.tm_year + 1900,
    tm_utc.tm_hour,
    tm_utc.tm_min,
    tm_utc.tm_sec,
    suffix);
}

static bool track_buffer_init(gps_points_feed_t feed)
{
  track_buffer_t* tb;
  bool ready;

  if (feed >= GPS_POINTS_FEED_COUNT)
  {
    return false;
  }

  if (!track_capacity_init())
  {
    return false;
  }

  tb = &s_track_buffers[feed];
  if (!track_buffer_ensure_mutex(tb))
  {
    return false;
  }

  if (xSemaphoreTake(s_track_capacity.mutex, pdMS_TO_TICKS(50)) != pdTRUE)
  {
    return false;
  }

  if (s_track_capacity.feed_active[feed] && tb->max_points == 0)
  {
    if (!track_capacity_rebalance_locked())
    {
      xSemaphoreGive(s_track_capacity.mutex);
      return false;
    }
  }

  ready = s_track_capacity.feed_active[feed] && tb->points != NULL && tb->max_points > 0;
  xSemaphoreGive(s_track_capacity.mutex);

  return ready;
}

bool gps_points_get_summary(gps_points_feed_t feed, size_t* out_points, time_t* out_first_utc)
{
  track_buffer_t* tb;

  if (out_points == NULL || out_first_utc == NULL)
  {
    return false;
  }

  *out_points = 0;
  *out_first_utc = 0;

  if (!track_buffer_init(feed))
  {
    return false;
  }

  tb = &s_track_buffers[feed];
  if (xSemaphoreTake(tb->mutex, pdMS_TO_TICKS(10)) != pdTRUE)
  {
    return false;
  }

  *out_points = tb->count;
  if (tb->count > 0)
  {
    *out_first_utc = tb->points[tb->head].utc_time;
  }

  xSemaphoreGive(tb->mutex);
  return true;
}

void gps_points_append(gps_points_feed_t feed, double lat_deg, double lon_deg, float ele_m, time_t utc_time)
{
  track_buffer_t* tb;
  uint64_t now_us;
  size_t idx;
  bool filter_stationary = GPS_POINTS_DEFAULT_FILTER_STATIONARY;
  bool filter_impossible = GPS_POINTS_DEFAULT_FILTER_IMPOSSIBLE;
  uint8_t stationary_radius_m = GPS_POINTS_DEFAULT_STATIONARY_RADIUS_M;

  if (!track_buffer_init(feed))
  {
    return;
  }

  if (gps_points_settings_ensure_loaded() &&
    xSemaphoreTake(s_gps_points_settings.mutex, pdMS_TO_TICKS(5)) == pdTRUE)
  {
    filter_stationary = s_gps_points_settings.config.filter_stationary_points;
    filter_impossible = s_gps_points_settings.config.filter_impossible_values;
    stationary_radius_m = s_gps_points_settings.config.stationary_radius_m;
    xSemaphoreGive(s_gps_points_settings.mutex);
  }

  if (filter_impossible && gps_points_value_is_impossible(lat_deg, lon_deg, ele_m, utc_time))
  {
    return;
  }

  tb = &s_track_buffers[feed];
  now_us = (uint64_t)esp_timer_get_time();
  if (xSemaphoreTake(tb->mutex, pdMS_TO_TICKS(2)) != pdTRUE)
  {
    return;
  }

  if (tb->last_store_us != 0 &&
    (now_us - tb->last_store_us) < ((uint64_t)TRACK_MIN_POINT_INTERVAL_MS * 1000ULL))
  {
    xSemaphoreGive(tb->mutex);
    return;
  }

  if (filter_stationary && gps_points_is_stationary_locked(tb, lat_deg, lon_deg, ele_m, stationary_radius_m))
  {
    xSemaphoreGive(tb->mutex);
    return;
  }

  if (tb->count < tb->max_points)
  {
    idx = (tb->head + tb->count) % tb->max_points;
    tb->count++;
  }
  else
  {
    idx = tb->head;
    tb->head = (tb->head + 1) % tb->max_points;
  }

  tb->points[idx].lat_deg = lat_deg;
  tb->points[idx].lon_deg = lon_deg;
  tb->points[idx].ele_m = ele_m;
  tb->points[idx].utc_time = utc_time;
  tb->last_store_us = now_us;

  xSemaphoreGive(tb->mutex);
}

static bool track_buffer_copy_snapshot(gps_points_feed_t feed, gps_point_t** out_points, size_t* out_count, time_t* out_first_utc)
{
  track_buffer_t* tb;
  gps_point_t* snapshot;
  size_t i;

  if (out_points == NULL || out_count == NULL || out_first_utc == NULL)
  {
    return false;
  }

  *out_points = NULL;
  *out_count = 0;
  *out_first_utc = 0;

  if (!track_buffer_init(feed))
  {
    return false;
  }

  tb = &s_track_buffers[feed];
  if (xSemaphoreTake(tb->mutex, pdMS_TO_TICKS(10)) != pdTRUE)
  {
    return false;
  }

  if (tb->count == 0)
  {
    xSemaphoreGive(tb->mutex);
    return true;
  }

  snapshot = heap_caps_malloc(tb->count * sizeof(gps_point_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (snapshot == NULL)
  {
    snapshot = heap_caps_malloc(tb->count * sizeof(gps_point_t), MALLOC_CAP_8BIT);
  }
  if (snapshot == NULL)
  {
    xSemaphoreGive(tb->mutex);
    return false;
  }

  for (i = 0; i < tb->count; ++i)
  {
    size_t idx = (tb->head + i) % tb->max_points;
    snapshot[i] = tb->points[idx];
  }

  *out_first_utc = snapshot[0].utc_time;
  *out_points = snapshot;
  *out_count = tb->count;
  xSemaphoreGive(tb->mutex);
  return true;
}

bool gps_points_write_gpx(gps_points_feed_t feed, const char* path, size_t* out_points, time_t* out_first_utc)
{
  FILE* f;
  gps_point_t* points = NULL;
  size_t count = 0;
  size_t i;
  time_t first_utc = 0;

  if (out_points)
  {
    *out_points = 0;
  }
  if (out_first_utc)
  {
    *out_first_utc = 0;
  }
  if (path == NULL || path[0] == '\0')
  {
    return false;
  }

  if (!track_buffer_copy_snapshot(feed, &points, &count, &first_utc))
  {
    return false;
  }

  f = fopen(path, "w");
  if (f == NULL)
  {
    if (points) free(points);
    return false;
  }

  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  fprintf(f, "<gpx version=\"1.1\" creator=\"RallyBox\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n");
  fprintf(f, "  <trk><name>RallyBox Track</name><trkseg>\n");

  for (i = 0; i < count; ++i)
  {
    struct tm tm_utc = { 0 };
    char iso[40];

    gmtime_r(&points[i].utc_time, &tm_utc);
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    fprintf(f,
      "    <trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%.2f</ele><time>%s</time></trkpt>\n",
      points[i].lat_deg,
      points[i].lon_deg,
      (double)points[i].ele_m,
      iso);
  }

  fprintf(f, "  </trkseg></trk>\n");
  fprintf(f, "</gpx>\n");
  fclose(f);

  if (out_points)
  {
    *out_points = count;
  }
  if (out_first_utc)
  {
    *out_first_utc = first_utc;
  }
  if (points) free(points);
  return true;
}

static bool track_expand_filename_template(const char* tmpl, const char* filename, char* out_url, size_t out_len)
{
  const char* token = "{filename}";
  const char* pos;

  if (tmpl == NULL || tmpl[0] == '\0' || filename == NULL || out_url == NULL || out_len == 0)
  {
    return false;
  }

  pos = strstr(tmpl, token);
  if (pos != NULL)
  {
    size_t left_len = (size_t)(pos - tmpl);
    size_t token_len = strlen(token);
    size_t right_len = strlen(pos + token_len);

    if (left_len + strlen(filename) + right_len + 1 > out_len)
    {
      return false;
    }

    memcpy(out_url, tmpl, left_len);
    out_url[left_len] = '\0';
    strcat(out_url, filename);
    strcat(out_url, pos + token_len);
    return true;
  }

  if (tmpl[strlen(tmpl) - 1] == '/')
  {
    return snprintf(out_url, out_len, "%s%s", tmpl, filename) < (int)out_len;
  }

  return snprintf(out_url, out_len, "%s", tmpl) < (int)out_len;
}

static bool track_copy_trimmed_http_url(const char* src, char* out_url, size_t out_len)
{
  const char* start;
  const char* end;
  size_t len;

  if (src == NULL || out_url == NULL || out_len == 0)
  {
    return false;
  }

  start = src;
  while (*start != '\0' && isspace((unsigned char)*start))
  {
    start++;
  }

  end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1]))
  {
    end--;
  }

  len = (size_t)(end - start);
  if (len == 0 || len + 1 > out_len)
  {
    return false;
  }
  if (strncmp(start, "https://", 8) != 0 && strncmp(start, "http://", 7) != 0)
  {
    return false;
  }

  memcpy(out_url, start, len);
  out_url[len] = '\0';
  return true;
}

static bool track_extract_json_url_value(const char* json, const char* key, char* out_url, size_t out_len)
{
  char pattern[32];
  const char* pos;
  const char* value_start;
  size_t written = 0;

  if (json == NULL || key == NULL || out_url == NULL || out_len == 0)
  {
    return false;
  }

  if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) >= (int)sizeof(pattern))
  {
    return false;
  }

  pos = strstr(json, pattern);
  if (pos == NULL)
  {
    return false;
  }

  pos += strlen(pattern);
  while (*pos != '\0' && isspace((unsigned char)*pos))
  {
    pos++;
  }
  if (*pos != ':')
  {
    return false;
  }
  pos++;
  while (*pos != '\0' && isspace((unsigned char)*pos))
  {
    pos++;
  }
  if (*pos != '"')
  {
    return false;
  }

  value_start = ++pos;
  (void)value_start;
  while (*pos != '\0')
  {
    char ch = *pos++;

    if (ch == '"')
    {
      out_url[written] = '\0';
      return strncmp(out_url, "https://", 8) == 0 || strncmp(out_url, "http://", 7) == 0;
    }

    if (ch == '\\')
    {
      ch = *pos++;
      if (ch == '\0')
      {
        return false;
      }
      if (ch == '/' || ch == '\\' || ch == '"')
      {
      }
      else if (ch == 'n')
      {
        ch = '\n';
      }
      else if (ch == 'r')
      {
        ch = '\r';
      }
      else if (ch == 't')
      {
        ch = '\t';
      }
      else
      {
        return false;
      }
    }

    if (written + 1 >= out_len)
    {
      return false;
    }
    out_url[written++] = ch;
  }

  return false;
}

static bool track_extract_signer_url_from_body(const char* body, char* out_url, size_t out_len)
{
  if (track_copy_trimmed_http_url(body, out_url, out_len))
  {
    return true;
  }

  if (track_extract_json_url_value(body, "upload_url", out_url, out_len))
  {
    return true;
  }

  return track_extract_json_url_value(body, "url", out_url, out_len);
}

static esp_err_t track_fetch_presigned_upload_url(const char* filename, char* out_url, size_t out_len)
{
  char request_url[640];
  esp_http_client_config_t cfg = { 0 };
  esp_http_client_handle_t client = NULL;
  char* resp_body = NULL;
  int body_len = 0;
  esp_err_t result = ESP_FAIL;

  if (!track_expand_filename_template(CONFIG_RALLYBOX_GPX_WEB_SIGNER_URL,
    filename,
    request_url,
    sizeof(request_url)))
  {
    return ESP_ERR_INVALID_ARG;
  }

  cfg.url = request_url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 15000;
#if RALLYBOX_HAS_CRT_BUNDLE
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

  client = esp_http_client_init(&cfg);
  if (client == NULL)
  {
    ESP_LOGE(TAG, "Signer request init failed");
    return ESP_FAIL;
  }

  resp_body = malloc(GPX_WEB_SIGNER_RESPONSE_MAX_LEN);
  if (resp_body == NULL)
  {
    ESP_LOGE(TAG, "Signer response buffer allocation failed");
    result = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  if (strlen(CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_HEADER) > 0 && strlen(CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_VALUE) > 0)
  {
    esp_http_client_set_header(client,
      CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_HEADER,
      CONFIG_RALLYBOX_GPX_WEB_SIGNER_AUTH_VALUE);
  }

  ESP_LOGI(TAG, "Requesting presigned GPX upload URL for filename=%s", filename);

  if (esp_http_client_open(client, 0) != ESP_OK)
  {
    ESP_LOGE(TAG, "Signer request open failed");
    goto cleanup;
  }

  (void)esp_http_client_fetch_headers(client);
  body_len = esp_http_client_read_response(client, resp_body, GPX_WEB_SIGNER_RESPONSE_MAX_LEN - 1);
  if (body_len < 0)
  {
    body_len = 0;
  }
  resp_body[body_len] = '\0';

  if (esp_http_client_get_status_code(client) < 200 || esp_http_client_get_status_code(client) >= 300)
  {
    ESP_LOGE(TAG,
      "Signer request failed: http=%d body=%s",
      esp_http_client_get_status_code(client),
      body_len > 0 ? resp_body : "<empty>");
    goto cleanup;
  }

  if (!track_extract_signer_url_from_body(resp_body, out_url, out_len))
  {
    ESP_LOGE(TAG,
      "Signer response did not contain a valid upload URL: body=%s",
      body_len > 0 ? resp_body : "<empty>");
    result = ESP_ERR_INVALID_RESPONSE;
    goto cleanup;
  }

  result = ESP_OK;

cleanup:
  if (client != NULL)
  {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
  }
  if (resp_body != NULL)
  {
    free(resp_body);
  }

  return result;
}

static bool track_build_web_url(const char* filename, char* out_url, size_t out_len)
{
  const char* tmpl = CONFIG_RALLYBOX_GPX_WEB_UPLOAD_URL;
  const char* token = "{filename}";
  const char* pos;
  const char* scheme;
  const char* host_start;
  const char* path_start;
  bool template_is_host_root = false;
  const char* s3_bucket = CONFIG_RALLYBOX_GPX_S3_BUCKET;
  const char* s3_region = CONFIG_RALLYBOX_GPX_S3_REGION;
  const char* s3_prefix = CONFIG_RALLYBOX_GPX_S3_OBJECT_PREFIX;
  const char* s3_query = CONFIG_RALLYBOX_GPX_S3_PRESIGNED_QUERY;

  if (out_url == NULL || out_len == 0 || filename == NULL)
  {
    return false;
  }
  if (tmpl != NULL && tmpl[0] != '\0')
  {
    pos = strstr(tmpl, token);
    if (pos)
    {
      size_t left_len = (size_t)(pos - tmpl);
      size_t token_len = strlen(token);
      size_t right_len = strlen(pos + token_len);
      if (left_len + strlen(filename) + right_len + 1 > out_len)
      {
        return false;
      }

      memcpy(out_url, tmpl, left_len);
      out_url[left_len] = '\0';
      strcat(out_url, filename);
      strcat(out_url, pos + token_len);
      track_try_normalize_s3_upload_url(out_url, out_len);
      return true;
    }

    if (tmpl[strlen(tmpl) - 1] == '/')
    {
      snprintf(out_url, out_len, "%s%s", tmpl, filename);
    }
    else
    {
      scheme = strstr(tmpl, "://");
      if (scheme)
      {
        host_start = scheme + 3;
        path_start = strchr(host_start, '/');
        template_is_host_root = (path_start == NULL) || (path_start[1] == '\0') || (path_start[1] == '?');
      }

      if (template_is_host_root && strchr(tmpl, '?') == NULL)
      {
        if (s3_bucket != NULL && s3_bucket[0] != '\0')
        {
          if (s3_prefix == NULL)
          {
            s3_prefix = "";
          }

          ESP_LOGW(TAG,
            "WEB upload URL points to endpoint root; auto-appending bucket/object path for filename=%s",
            filename);
          snprintf(out_url,
            out_len,
            "%s%s%s/%s%s%s",
            tmpl,
            (tmpl[strlen(tmpl) - 1] == '/') ? "" : "/",
            s3_bucket,
            s3_prefix,
            (s3_prefix[0] != '\0' && s3_prefix[strlen(s3_prefix) - 1] != '/') ? "/" : "",
            filename);
        }
        else
        {
          snprintf(out_url, out_len, "%s/%s", tmpl, filename);
        }
      }
      else
      {
        snprintf(out_url, out_len, "%s", tmpl);
      }
    }
    track_try_normalize_s3_upload_url(out_url, out_len);
    return true;
  }

  if (s3_bucket == NULL || s3_bucket[0] == '\0')
  {
    return false;
  }

  if (s3_region == NULL || s3_region[0] == '\0')
  {
    s3_region = "us-east-1";
  }
  if (s3_prefix == NULL)
  {
    s3_prefix = "";
  }

#if CONFIG_RALLYBOX_GPX_S3_VIRTUAL_HOSTED_STYLE
  snprintf(out_url,
    out_len,
    "%s://%s.s3.%s.amazonaws.com/%s%s%s",
#if CONFIG_RALLYBOX_GPX_S3_USE_HTTPS
    "https",
#else
    "http",
#endif
    s3_bucket,
    s3_region,
    s3_prefix,
    (s3_prefix[0] != '\0' && s3_prefix[strlen(s3_prefix) - 1] != '/') ? "/" : "",
    filename);
#else
  snprintf(out_url,
    out_len,
    "%s://s3.%s.amazonaws.com/%s/%s%s%s",
#if CONFIG_RALLYBOX_GPX_S3_USE_HTTPS
    "https",
#else
    "http",
#endif
    s3_region,
    s3_bucket,
    s3_prefix,
    (s3_prefix[0] != '\0' && s3_prefix[strlen(s3_prefix) - 1] != '/') ? "/" : "",
    filename);
#endif

  if (s3_query != NULL && s3_query[0] != '\0')
  {
    size_t used = strlen(out_url);
    if (used + strlen(s3_query) + 2 <= out_len)
    {
      snprintf(out_url + used, out_len - used, "%c%s", strchr(out_url, '?') ? '&' : '?', s3_query);
    }
  }

  return true;
}

static esp_err_t track_resolve_web_upload_url(const char* filename, char* out_url, size_t out_len)
{
  if (filename == NULL || filename[0] == '\0' || out_url == NULL || out_len == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (CONFIG_RALLYBOX_GPX_WEB_SIGNER_URL[0] != '\0')
  {
    return track_fetch_presigned_upload_url(filename, out_url, out_len);
  }

  return track_build_web_url(filename, out_url, out_len) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static void track_try_normalize_s3_upload_url(char* url, size_t url_len)
{
#if CONFIG_RALLYBOX_GPX_S3_VIRTUAL_HOSTED_STYLE
  char host[GPX_WEB_HOST_MAX_LEN];
  char uri[GPX_WEB_URI_MAX_LEN];
  char expected_host[128];
  char* query = NULL;
  char* normalized = NULL;
  const char* bucket = CONFIG_RALLYBOX_GPX_S3_BUCKET;
  const char* region = CONFIG_RALLYBOX_GPX_S3_REGION;
  const char* scheme = "https";
  const char* key_path;
  size_t bucket_len;

  if (url == NULL || url_len == 0)
  {
    return;
  }
  if (bucket == NULL || bucket[0] == '\0')
  {
    return;
  }
  if (region == NULL || region[0] == '\0')
  {
    region = "us-east-1";
  }
  if (strstr(url, "X-Amz-Algorithm=") != NULL)
  {
    return;
  }
  query = malloc(GPX_WEB_QUERY_MAX_LEN);
  normalized = malloc(GPX_WEB_URL_MAX_LEN);
  if (query == NULL || normalized == NULL)
  {
    goto cleanup;
  }
  if (!sigv4_parse_url_parts(url, host, sizeof(host), uri, sizeof(uri), query, GPX_WEB_QUERY_MAX_LEN))
  {
    goto cleanup;
  }
  snprintf(expected_host, sizeof(expected_host), "s3.%s.amazonaws.com", region);
  if (strcmp(host, expected_host) != 0)
  {
    goto cleanup;
  }
  bucket_len = strlen(bucket);
  if (strncmp(uri, "/", 1) != 0 || strncmp(uri + 1, bucket, bucket_len) != 0)
  {
    goto cleanup;
  }
  key_path = uri + 1 + bucket_len;
  if (*key_path == '\0')
  {
    key_path = "/";
  }
  else if (*key_path != '/')
  {
    goto cleanup;
  }
  if (strncmp(url, "http://", 7) == 0)
  {
    scheme = "http";
  }
  snprintf(normalized,
    GPX_WEB_URL_MAX_LEN,
    "%s://%s.%s%s%s%s",
    scheme,
    bucket,
    expected_host,
    key_path,
    query[0] != '\0' ? "?" : "",
    query);
  if (strlen(normalized) + 1 > url_len)
  {
    goto cleanup;
  }
  snprintf(url, url_len, "%s", normalized);
  ESP_LOGI(TAG, "WEB upload normalized to virtual-hosted S3 URL: %s", url);

cleanup:
  if (normalized != NULL)
  {
    free(normalized);
  }
  if (query != NULL)
  {
    free(query);
  }
#else
  (void)url;
  (void)url_len;
#endif
}

static bool sigv4_parse_url_parts(const char* url, char* out_host, size_t out_host_len, char* out_uri, size_t out_uri_len, char* out_query, size_t out_query_len)
{
  const char* scheme;
  const char* host_start;
  const char* path_start;
  const char* query_start;
  size_t host_len;
  size_t uri_len;

  if (url == NULL || out_host == NULL || out_uri == NULL || out_query == NULL)
  {
    return false;
  }

  scheme = strstr(url, "://");
  host_start = scheme ? (scheme + 3) : url;
  path_start = strchr(host_start, '/');

  if (path_start)
  {
    host_len = (size_t)(path_start - host_start);
  }
  else
  {
    host_len = strlen(host_start);
  }

  if (host_len == 0 || host_len + 1 > out_host_len)
  {
    return false;
  }

  memcpy(out_host, host_start, host_len);
  out_host[host_len] = '\0';

  if (path_start == NULL)
  {
    if (out_uri_len < 2)
    {
      return false;
    }
    strcpy(out_uri, "/");
    out_query[0] = '\0';
    return true;
  }

  query_start = strchr(path_start, '?');
  if (query_start)
  {
    uri_len = (size_t)(query_start - path_start);
  }
  else
  {
    uri_len = strlen(path_start);
  }

  if (uri_len == 0 || uri_len + 1 > out_uri_len)
  {
    return false;
  }

  memcpy(out_uri, path_start, uri_len);
  out_uri[uri_len] = '\0';

  if (query_start)
  {
    size_t query_len = strlen(query_start + 1);
    if (query_len + 1 > out_query_len)
    {
      return false;
    }
    memcpy(out_query, query_start + 1, query_len);
    out_query[query_len] = '\0';
  }
  else
  {
    out_query[0] = '\0';
  }

  return true;
}

static esp_err_t track_upload_gpx_snapshot_to_url(gps_points_feed_t feed, const char* url, size_t* out_points, time_t* out_first_utc)
{
  esp_http_client_config_t cfg = { 0 };
  esp_http_client_handle_t client = NULL;
  gps_point_t* points = NULL;
  size_t count = 0;
  size_t i;
  int total_len = 0;
  const char* gpx_head = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<gpx version=\"1.1\" creator=\"RallyBox\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
    "  <trk><name>RallyBox Track</name><trkseg>\n";
  const char* gpx_tail = "  </trkseg></trk>\n</gpx>\n";
  int status;
  int body_len = 0;
  char resp_body[256] = { 0 };
  time_t first_utc = 0;
  char request_host[GPX_WEB_HOST_MAX_LEN];
  char request_uri[GPX_WEB_URI_MAX_LEN];
  char* request_query = NULL;
  char* request_path = NULL;
  bool use_https = false;
  bool client_open = false;
  esp_err_t result = ESP_FAIL;

  if (out_points)
  {
    *out_points = 0;
  }
  if (out_first_utc)
  {
    *out_first_utc = 0;
  }

  if (url == NULL || url[0] == '\0')
  {
    return ESP_ERR_INVALID_ARG;
  }
  request_query = malloc(GPX_WEB_QUERY_MAX_LEN);
  request_path = malloc(GPX_WEB_REQUEST_PATH_MAX_LEN);
  if (request_query == NULL || request_path == NULL)
  {
    result = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  if (!sigv4_parse_url_parts(url,
    request_host,
    sizeof(request_host),
    request_uri,
    sizeof(request_uri),
    request_query,
    GPX_WEB_QUERY_MAX_LEN))
  {
    ESP_LOGE(TAG, "WEB upload failed to parse URL: %s", url);
    result = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }
  use_https = (strncmp(url, "https://", 8) == 0);
  snprintf(request_path,
    GPX_WEB_REQUEST_PATH_MAX_LEN,
    "%s%s%s",
    request_uri,
    request_query[0] != '\0' ? "?" : "",
    request_query);
  if (!track_buffer_copy_snapshot(feed, &points, &count, &first_utc))
  {
    result = ESP_FAIL;
    goto cleanup;
  }

  total_len += (int)strlen(gpx_head);
  total_len += (int)strlen(gpx_tail);
  for (i = 0; i < count; ++i)
  {
    struct tm tm_utc = { 0 };
    char iso[40];
    gmtime_r(&points[i].utc_time, &tm_utc);
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    total_len += snprintf(NULL, 0,
      "    <trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%.2f</ele><time>%s</time></trkpt>\n",
      points[i].lat_deg,
      points[i].lon_deg,
      (double)points[i].ele_m,
      iso);
  }

  cfg.host = request_host;
  cfg.path = request_path;
  cfg.method = HTTP_METHOD_PUT;
  cfg.port = use_https ? 443 : 80;
  cfg.transport_type = use_https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
  cfg.timeout_ms = 20000;
  cfg.buffer_size = GPX_WEB_HTTP_RX_BUFFER_LEN;
  cfg.buffer_size_tx = GPX_WEB_HTTP_TX_BUFFER_LEN;
  cfg.keep_alive_enable = true;
#if RALLYBOX_HAS_CRT_BUNDLE
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  client = esp_http_client_init(&cfg);
  if (client == NULL)
  {
    goto cleanup;
  }

  esp_http_client_set_header(client, "Content-Type", "application/gpx+xml");
  ESP_LOGI(TAG, "WEB upload start: mode=%s url=%s points=%u bytes=%d",
    "plain",
    url,
    (unsigned)count,
    total_len);
  if (strlen(CONFIG_RALLYBOX_GPX_WEB_AUTH_HEADER) > 0 && strlen(CONFIG_RALLYBOX_GPX_WEB_AUTH_VALUE) > 0)
  {
    esp_http_client_set_header(client,
      CONFIG_RALLYBOX_GPX_WEB_AUTH_HEADER,
      CONFIG_RALLYBOX_GPX_WEB_AUTH_VALUE);
  }

  ESP_LOGI(TAG, "free internal: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  ESP_LOGI(TAG, "largest internal: %u", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  ESP_LOGI(TAG, "free 8bit: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
  ESP_LOGI(TAG, "min internal: %u", (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

  if (esp_http_client_open(client, total_len) != ESP_OK)
  {
    ESP_LOGE(TAG, "WEB upload open failed");
    goto cleanup;
  }
  client_open = true;

  if (esp_http_client_write(client, gpx_head, (int)strlen(gpx_head)) < 0)
  {
    ESP_LOGE(TAG, "WEB upload write failed at GPX header");
    goto cleanup;
  }
  for (i = 0; i < count; ++i)
  {
    struct tm tm_utc = { 0 };
    char iso[40];
    char line[192];
    int n;

    gmtime_r(&points[i].utc_time, &tm_utc);
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    n = snprintf(line, sizeof(line),
      "    <trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%.2f</ele><time>%s</time></trkpt>\n",
      points[i].lat_deg,
      points[i].lon_deg,
      (double)points[i].ele_m,
      iso);
    if (n > 0)
    {
      if (esp_http_client_write(client, line, n) < 0)
      {
        ESP_LOGE(TAG, "WEB upload write failed at GPX point %u", (unsigned)i);
        goto cleanup;
      }
    }
  }
  if (esp_http_client_write(client, gpx_tail, (int)strlen(gpx_tail)) < 0)
  {
    ESP_LOGE(TAG, "WEB upload write failed at GPX footer");
    goto cleanup;
  }

  status = esp_http_client_fetch_headers(client);
  (void)status;
  if (out_points)
  {
    *out_points = count;
  }
  if (out_first_utc)
  {
    *out_first_utc = first_utc;
  }

  status = esp_http_client_get_status_code(client);
  body_len = esp_http_client_read_response(client, resp_body, sizeof(resp_body) - 1);
  if (body_len < 0)
  {
    body_len = 0;
  }
  resp_body[body_len] = '\0';

  ESP_LOGI(TAG, "WEB upload result: http=%d resp_len=%d", status, body_len);
  if (status < 200 || status >= 300)
  {
    ESP_LOGE(TAG, "WEB upload failed: http=%d body=%s", status, body_len > 0 ? resp_body : "<empty>");
  }

  if (status >= 200 && status < 300)
  {
    result = ESP_OK;
  }

cleanup:
  if (client_open)
  {
    esp_http_client_close(client);
  }
  if (client != NULL)
  {
    esp_http_client_cleanup(client);
  }
  if (points)
  {
    free(points);
  }
  if (request_path != NULL)
  {
    free(request_path);
  }
  if (request_query != NULL)
  {
    free(request_query);
  }
  return result;
}

esp_err_t gps_points_upload_web(gps_points_feed_t feed, const char* filename, size_t* out_points, time_t* out_first_utc)
{
  char* url = NULL;
  esp_err_t resolve_result;

  url = malloc(GPX_WEB_URL_MAX_LEN);
  if (url == NULL)
  {
    return ESP_ERR_NO_MEM;
  }

  resolve_result = track_resolve_web_upload_url(filename, url, GPX_WEB_URL_MAX_LEN);
  if (resolve_result != ESP_OK)
  {
    free(url);
    return resolve_result;
  }

  resolve_result = track_upload_gpx_snapshot_to_url(feed, url, out_points, out_first_utc);
  free(url);
  return resolve_result;
}