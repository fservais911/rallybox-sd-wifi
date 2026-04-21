#ifndef GPS_POINTS_H
#define GPS_POINTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct
  {
    double lat_deg;
    double lon_deg;
    float ele_m;
    time_t utc_time;
  } gps_point_t;

  typedef enum
  {
    GPS_POINTS_FEED_BLE = 0,
    GPS_POINTS_FEED_GNSS,
    GPS_POINTS_FEED_COUNT
  } gps_points_feed_t;

  typedef struct
  {
    bool filter_stationary_points;
    bool filter_impossible_values;
    uint8_t stationary_radius_m;
  } gps_points_filter_config_t;

  void gps_points_set_feed_active(gps_points_feed_t feed, bool active);
  bool gps_points_is_feed_active(gps_points_feed_t feed);
  esp_err_t gps_points_reset_feed(gps_points_feed_t feed);
  bool gps_points_get_usage(gps_points_feed_t feed, size_t* out_points, size_t* out_capacity_points, uint8_t* out_fill_percent);
  bool gps_points_get_filter_config(gps_points_filter_config_t* out_config);
  esp_err_t gps_points_set_filter_config(const gps_points_filter_config_t* config);
  void gps_points_append(gps_points_feed_t feed, double lat_deg, double lon_deg, float ele_m, time_t utc_time);
  bool gps_points_get_summary(gps_points_feed_t feed, size_t* out_points, time_t* out_first_utc);
  bool gps_points_write_gpx(gps_points_feed_t feed, const char* path, size_t* out_points, time_t* out_first_utc);
  esp_err_t gps_points_upload_web(gps_points_feed_t feed, const char* filename, size_t* out_points, time_t* out_first_utc);
  void gps_points_make_filename(gps_points_feed_t feed, time_t first_utc, char* out_name, size_t out_len);

  size_t gps_points_get_configured_max_points(void);
  size_t gps_points_get_point_size_bytes(void);
  size_t gps_points_get_configured_bytes_per_feed(void);
  size_t gps_points_get_configured_bytes_total(void);

#ifdef __cplusplus
}
#endif

#endif