#include "racebox.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "esp_bluedroid_hci.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gattc_api.h"
#include "esp_hosted.h"
#include "esp_hosted_bluedroid.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "system_monitor.h"

static const char* TAG = "RaceBox";
static const char* TARGET_NAME_A = CONFIG_RALLYBOX_RACEBOX_TARGET_NAME;
static const char* TARGET_NAME_B = CONFIG_RALLYBOX_RACEBOX_ALT_TARGET_NAME;

static bool s_racebox_initialized = false;
static bool s_racebox_connecting = false;
static bool s_racebox_connected = false;
static esp_gatt_if_t s_gattc_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id = 0xFFFF;
static esp_bd_addr_t s_peer_bda = { 0 };
static esp_ble_addr_type_t s_peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
static int16_t s_last_rssi = 0;
static char s_device_name[32] = "";
static bool s_scan_active = false;
static racebox_rx_callback_t s_rx_callback = NULL;
static void* s_rx_callback_user_ctx = NULL;
static uint16_t s_service_start_handle = 0;
static uint16_t s_service_end_handle = 0;
static uint16_t s_notify_char_handles[8] = { 0 };
static size_t s_notify_char_count = 0;
static const esp_bt_uuid_t notify_descr_uuid = {
  .len = ESP_UUID_LEN_16,
  .uuid = {
    .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,
  },
};

typedef struct
{
  bool in_use;
  esp_bd_addr_t bda;
  esp_ble_addr_type_t addr_type;
  int16_t rssi;
  char name[32];
} racebox_visible_entry_t;

static racebox_visible_entry_t s_visible_devices[RACEBOX_VISIBLE_DEVICES_MAX];
static size_t s_visible_count = 0;
static SemaphoreHandle_t s_visible_mutex = NULL;

static const char* racebox_gatt_status_name(esp_gatt_status_t status)
{
  int status_code = (int)status;

  if (status_code == (int)ESP_GATT_OK)
  {
    return "ESP_GATT_OK";
  }
  if (status_code == (int)ESP_GATT_ERROR)
  {
    return "ESP_GATT_ERROR";
  }
  if (status_code == (int)ESP_GATT_BUSY)
  {
    return "ESP_GATT_BUSY";
  }
  if (status_code == (int)ESP_GATT_NO_RESOURCES)
  {
    return "ESP_GATT_NO_RESOURCES";
  }
  if (status_code == (int)ESP_GATT_INTERNAL_ERROR)
  {
    return "ESP_GATT_INTERNAL_ERROR";
  }
  if (status_code == (int)ESP_GATT_WRONG_STATE)
  {
    return "ESP_GATT_WRONG_STATE";
  }
  if (status_code == (int)ESP_GATT_DB_FULL)
  {
    return "ESP_GATT_DB_FULL";
  }
  if (status_code == (int)ESP_GATT_AUTH_FAIL)
  {
    return "ESP_GATT_AUTH_FAIL";
  }
  if (status_code == (int)ESP_GATT_CONN_TIMEOUT)
  {
    return "ESP_GATT_CONN_TIMEOUT";
  }
  if (status_code == (int)ESP_GATT_CONN_FAIL_ESTABLISH)
  {
    return "ESP_GATT_CONN_FAIL_ESTABLISH";
  }
  if (status_code == (int)ESP_GATT_CONN_L2C_FAILURE)
  {
    return "ESP_GATT_CONN_L2C_FAILURE";
  }
  return "ESP_GATT_STATUS_UNKNOWN";
}

static esp_ble_scan_params_t s_scan_params = {
  .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
  .scan_interval = CONFIG_RALLYBOX_RACEBOX_SCAN_INTERVAL,
  .scan_window = CONFIG_RALLYBOX_RACEBOX_SCAN_WINDOW,
  .scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE,
};

static bool racebox_ensure_visible_mutex(void);
static void racebox_start_scan(void);
static void racebox_copy_adv_name(char* dst, size_t dst_len, const uint8_t* adv_name, uint8_t adv_name_len);
static void racebox_update_status(bool connected, int16_t rssi, const char* device_name, const char* status_text);

static bool racebox_contains_ignore_case(const char* haystack, const char* needle)
{
  size_t i;
  size_t needle_len;

  if (!haystack || !needle)
  {
    return false;
  }

  needle_len = strlen(needle);
  if (needle_len == 0)
  {
    return false;
  }

  for (i = 0; haystack[i] != '\0'; ++i)
  {
    size_t j = 0;
    while (haystack[i + j] != '\0' && j < needle_len)
    {
      char hc = (char)tolower((unsigned char)haystack[i + j]);
      char nc = (char)tolower((unsigned char)needle[j]);
      if (hc != nc)
      {
        break;
      }
      ++j;
    }
    if (j == needle_len)
    {
      return true;
    }
  }

  return false;
}

static void racebox_clear_visible_devices(void)
{
  if (!racebox_ensure_visible_mutex())
  {
    return;
  }

  if (xSemaphoreTake(s_visible_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
  {
    return;
  }

  memset(s_visible_devices, 0, sizeof(s_visible_devices));
  s_visible_count = 0;

  xSemaphoreGive(s_visible_mutex);
}

static bool racebox_ensure_visible_mutex(void)
{
  if (s_visible_mutex == NULL)
  {
    s_visible_mutex = xSemaphoreCreateMutex();
  }
  return s_visible_mutex != NULL;
}

static void racebox_format_address(const esp_bd_addr_t bda, char* out, size_t out_len)
{
  if (!out || out_len == 0)
  {
    return;
  }

  if (!bda)
  {
    out[0] = '\0';
    return;
  }

  snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
    bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static int racebox_find_visible_device(const esp_bd_addr_t bda)
{
  size_t i;

  for (i = 0; i < s_visible_count; ++i)
  {
    if (s_visible_devices[i].in_use && memcmp(s_visible_devices[i].bda, bda, sizeof(esp_bd_addr_t)) == 0)
    {
      return (int)i;
    }
  }

  return -1;
}

static void racebox_upsert_visible_device(const esp_ble_gap_cb_param_t* param, const uint8_t* adv_name, uint8_t adv_name_len)
{
  int idx;
  size_t slot;

  if (!param)
  {
    return;
  }

  if (!racebox_ensure_visible_mutex())
  {
    return;
  }

  if (xSemaphoreTake(s_visible_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
  {
    return;
  }

  idx = racebox_find_visible_device(param->scan_rst.bda);
  if (idx >= 0)
  {
    slot = (size_t)idx;
  }
  else if (s_visible_count < RACEBOX_VISIBLE_DEVICES_MAX)
  {
    slot = s_visible_count;
    s_visible_count++;
  }
  else
  {
    size_t i;
    size_t weakest_idx = 0;
    int16_t weakest_rssi = s_visible_devices[0].rssi;

    for (i = 1; i < s_visible_count; ++i)
    {
      if (s_visible_devices[i].rssi < weakest_rssi)
      {
        weakest_rssi = s_visible_devices[i].rssi;
        weakest_idx = i;
      }
    }

    slot = weakest_idx;
  }

  s_visible_devices[slot].in_use = true;
  memcpy(s_visible_devices[slot].bda, param->scan_rst.bda, sizeof(esp_bd_addr_t));
  s_visible_devices[slot].addr_type = param->scan_rst.ble_addr_type;
  s_visible_devices[slot].rssi = param->scan_rst.rssi;
  racebox_copy_adv_name(s_visible_devices[slot].name, sizeof(s_visible_devices[slot].name), adv_name, adv_name_len);

  xSemaphoreGive(s_visible_mutex);
}

static esp_err_t racebox_open_pending_connection(void)
{
  esp_err_t open_ret;

  if (!s_racebox_connecting || s_gattc_if == ESP_GATT_IF_NONE)
  {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG,
    "Opening GATT connection to %s addr=%02x:%02x:%02x:%02x:%02x:%02x rssi=%d",
    s_device_name[0] != '\0' ? s_device_name : "<unknown>",
    s_peer_bda[0], s_peer_bda[1], s_peer_bda[2], s_peer_bda[3], s_peer_bda[4], s_peer_bda[5],
    s_last_rssi);
  open_ret = esp_ble_gattc_open(s_gattc_if, s_peer_bda, s_peer_addr_type, true);
  if (open_ret != ESP_OK)
  {
    s_racebox_connecting = false;
    ESP_LOGE(TAG, "esp_ble_gattc_open() failed immediately: %s", esp_err_to_name(open_ret));
    racebox_update_status(false, s_last_rssi, s_device_name, "RaceBox GATT open failed to start");
    racebox_start_scan();
    return open_ret;
  }

  return ESP_OK;
}

static void racebox_copy_adv_name(char* dst, size_t dst_len, const uint8_t* adv_name, uint8_t adv_name_len)
{
  size_t copy_len;

  if (!dst || dst_len == 0)
  {
    return;
  }

  dst[0] = '\0';
  if (!adv_name || adv_name_len == 0)
  {
    return;
  }

  copy_len = adv_name_len < (dst_len - 1) ? adv_name_len : (dst_len - 1);
  memcpy(dst, adv_name, copy_len);
  dst[copy_len] = '\0';
}

static void racebox_log_scan_result(const esp_ble_gap_cb_param_t* param, const uint8_t* adv_name, uint8_t adv_name_len)
{
  char name[32];

  if (!param)
  {
    return;
  }

  racebox_copy_adv_name(name, sizeof(name), adv_name, adv_name_len);
  ESP_LOGI(TAG,
    "Scan hit addr=%02x:%02x:%02x:%02x:%02x:%02x type=%u rssi=%d name=%s",
    param->scan_rst.bda[0], param->scan_rst.bda[1], param->scan_rst.bda[2],
    param->scan_rst.bda[3], param->scan_rst.bda[4], param->scan_rst.bda[5],
    param->scan_rst.ble_addr_type,
    param->scan_rst.rssi,
    name[0] != '\0' ? name : "<no-name>");
}

static bool racebox_name_matches(const uint8_t* adv_name, uint8_t adv_name_len)
{
  char name[32] = { 0 };
  size_t copy_len;

  if (!adv_name || adv_name_len == 0)
  {
    return false;
  }

  copy_len = adv_name_len < (sizeof(name) - 1) ? adv_name_len : (sizeof(name) - 1);
  memcpy(name, adv_name, copy_len);
  return racebox_contains_ignore_case(name, TARGET_NAME_A) || racebox_contains_ignore_case(name, TARGET_NAME_B);
}

static void racebox_update_status(bool connected, int16_t rssi, const char* device_name, const char* status_text)
{
  system_monitor_set_racebox_status(s_racebox_initialized, connected, rssi, device_name, status_text);
}

static void racebox_record_notify_char(uint16_t handle)
{
  size_t i;

  if (handle == 0)
  {
    return;
  }

  for (i = 0; i < s_notify_char_count; ++i)
  {
    if (s_notify_char_handles[i] == handle)
    {
      return;
    }
  }

  if (s_notify_char_count < (sizeof(s_notify_char_handles) / sizeof(s_notify_char_handles[0])))
  {
    s_notify_char_handles[s_notify_char_count++] = handle;
  }
}

static void racebox_subscribe_all_notify_chars(esp_gatt_if_t gattc_if, uint16_t conn_id)
{
  uint16_t count = 0;
  esp_err_t err;

  if (s_service_start_handle == 0 || s_service_end_handle == 0 || s_service_start_handle >= s_service_end_handle)
  {
    racebox_update_status(true, s_last_rssi, s_device_name, "Connected; no discoverable service range");
    return;
  }

  err = esp_ble_gattc_get_attr_count(gattc_if,
    conn_id,
    ESP_GATT_DB_CHARACTERISTIC,
    s_service_start_handle,
    s_service_end_handle,
    0,
    &count);
  if (err != ESP_OK || count == 0)
  {
    racebox_update_status(true, s_last_rssi, s_device_name, "Connected; no characteristics found");
    return;
  }

  esp_gattc_char_elem_t* chars = (esp_gattc_char_elem_t*)calloc(count, sizeof(esp_gattc_char_elem_t));
  if (chars == NULL)
  {
    racebox_update_status(true, s_last_rssi, s_device_name, "Connected; characteristic alloc failed");
    return;
  }

  err = esp_ble_gattc_get_all_char(gattc_if,
    conn_id,
    s_service_start_handle,
    s_service_end_handle,
    chars,
    &count,
    0);
  if (err == ESP_OK)
  {
    uint16_t i;
    for (i = 0; i < count; ++i)
    {
      if ((chars[i].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) ||
        (chars[i].properties & ESP_GATT_CHAR_PROP_BIT_INDICATE))
      {
        racebox_record_notify_char(chars[i].char_handle);
        esp_ble_gattc_register_for_notify(gattc_if, s_peer_bda, chars[i].char_handle);
      }
    }
  }

  free(chars);

  if (s_notify_char_count > 0)
  {
    racebox_update_status(true, s_last_rssi, s_device_name, "Connected; subscribed for RaceBox data");
  }
  else
  {
    racebox_update_status(true, s_last_rssi, s_device_name, "Connected; no notify chars found");
  }
}

static void racebox_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param)
{
  switch (event)
  {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
      if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS)
      {
        ESP_LOGI(TAG, "BLE scan parameters configured");
        racebox_start_scan();
      }
      else
      {
        ESP_LOGE(TAG, "BLE scan parameter setup failed, status=%d", param->scan_param_cmpl.status);
        racebox_update_status(false, 0, s_device_name, "BLE scan parameter setup failed");
      }
      break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
      if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
      {
        s_scan_active = true;
        ESP_LOGI(TAG, "BLE scan started for %d seconds", CONFIG_RALLYBOX_RACEBOX_SCAN_DURATION_SECONDS);
        racebox_update_status(false, 0, s_device_name, "Scanning for RaceBox");
      }
      else
      {
        ESP_LOGE(TAG, "Failed to start BLE scan, status=%d", param->scan_start_cmpl.status);
        racebox_update_status(false, 0, s_device_name, "Failed to start BLE scan");
      }
      break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
      if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
      {
        ESP_LOGE(TAG, "Failed to stop BLE scan, status=%d", param->scan_stop_cmpl.status);
        racebox_update_status(false, 0, s_device_name, "Failed to stop BLE scan");
        break;
      }

      s_scan_active = false;

      if (s_racebox_connecting && s_gattc_if != ESP_GATT_IF_NONE)
      {
        racebox_open_pending_connection();
      }
      else if (!s_racebox_connected)
      {
        ESP_LOGI(TAG, "BLE scan stopped without target connection, restarting scan");
        racebox_start_scan();
      }
      break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
      if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT && !s_racebox_connected && !s_racebox_connecting)
      {
        esp_err_t stop_ret;
        uint8_t adv_name_len = 0;
        uint8_t* adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
          ESP_BLE_AD_TYPE_NAME_CMPL,
          &adv_name_len);
        if (adv_name == NULL)
        {
          adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
            ESP_BLE_AD_TYPE_NAME_SHORT,
            &adv_name_len);
        }

        racebox_log_scan_result(param, adv_name, adv_name_len);
        racebox_upsert_visible_device(param, adv_name, adv_name_len);

        if (racebox_name_matches(adv_name, adv_name_len))
        {
          char matched_name[32] = { 0 };
          racebox_copy_adv_name(matched_name, sizeof(matched_name), adv_name, adv_name_len);

          memcpy(s_peer_bda, param->scan_rst.bda, sizeof(s_peer_bda));
          s_peer_addr_type = param->scan_rst.ble_addr_type;
          s_last_rssi = param->scan_rst.rssi;
          snprintf(s_device_name, sizeof(s_device_name), "%s",
            matched_name[0] != '\0' ? matched_name : "RaceBox");

          s_racebox_connecting = true;
          racebox_update_status(false, param->scan_rst.rssi,
            matched_name[0] != '\0' ? matched_name : "RaceBox",
            "RaceBox matched; connecting...");

          if (s_scan_active)
          {
            stop_ret = esp_ble_gap_stop_scanning();
            if (stop_ret != ESP_OK)
            {
              s_racebox_connecting = false;
              ESP_LOGE(TAG, "Failed to stop scan before connect: %s", esp_err_to_name(stop_ret));
              racebox_start_scan();
            }
          }
          else
          {
            racebox_open_pending_connection();
          }
        }
      }
      else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT && !s_racebox_connected && !s_racebox_connecting)
      {
        ESP_LOGI(TAG, "BLE scan window completed without a connection");
        racebox_start_scan();
      }
      break;
    default:
      break;
  }
}

static void racebox_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
  esp_ble_gattc_cb_param_t* param)
{
  switch (event)
  {
    case ESP_GATTC_REG_EVT:
      if (param->reg.status == ESP_GATT_OK)
      {
        s_gattc_if = gattc_if;
        ESP_LOGI(TAG, "GATTC app registered, setting BLE scan parameters");
        esp_ble_gap_set_scan_params(&s_scan_params);
      }
      else
      {
        ESP_LOGE(TAG, "RaceBox GATTC registration failed, status=%d", param->reg.status);
        racebox_update_status(false, 0, s_device_name, "RaceBox GATTC registration failed");
      }
      break;
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK)
      {
        s_racebox_connected = true;
        s_racebox_connecting = false;
        s_conn_id = param->open.conn_id;
        s_service_start_handle = 0;
        s_service_end_handle = 0;
        s_notify_char_count = 0;
        memset(s_notify_char_handles, 0, sizeof(s_notify_char_handles));
        ESP_LOGI(TAG, "Connected to %s conn_id=%u mtu_pending", s_device_name[0] != '\0' ? s_device_name : "<unknown>", param->open.conn_id);
        racebox_update_status(true, s_last_rssi, s_device_name, "Connected to RaceBox");
        esp_ble_gattc_search_service(gattc_if, param->open.conn_id, NULL);
      }
      else
      {
        s_racebox_connecting = false;
        ESP_LOGE(TAG, "RaceBox connect failed, status=%d (%s)",
          param->open.status,
          racebox_gatt_status_name(param->open.status));
        racebox_update_status(false, s_last_rssi, s_device_name, "RaceBox connect failed");
        racebox_start_scan();
      }
      break;
    case ESP_GATTC_SEARCH_RES_EVT:
      ESP_LOGI(TAG, "Discovered RaceBox service len=%u", param->search_res.srvc_id.uuid.len);
      if (s_service_start_handle == 0 ||
        (param->search_res.start_handle < s_service_start_handle))
      {
        s_service_start_handle = param->search_res.start_handle;
      }
      if (param->search_res.end_handle > s_service_end_handle)
      {
        s_service_end_handle = param->search_res.end_handle;
      }
      break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
      ESP_LOGI(TAG, "Service discovery complete for %s", s_device_name[0] != '\0' ? s_device_name : "<unknown>");
      racebox_subscribe_all_notify_chars(gattc_if, param->search_cmpl.conn_id);
      break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.status == ESP_GATT_OK)
      {
        uint16_t descr_count = 0;
        esp_err_t err = esp_ble_gattc_get_attr_count(gattc_if,
          s_conn_id,
          ESP_GATT_DB_DESCRIPTOR,
          s_service_start_handle,
          s_service_end_handle,
          param->reg_for_notify.handle,
          &descr_count);

        if (err == ESP_OK && descr_count > 0)
        {
          esp_gattc_descr_elem_t* descrs = (esp_gattc_descr_elem_t*)calloc(descr_count, sizeof(esp_gattc_descr_elem_t));
          if (descrs)
          {
            uint16_t cfg = 0x0001;
            if (esp_ble_gattc_get_descr_by_char_handle(gattc_if,
              s_conn_id,
              param->reg_for_notify.handle,
              notify_descr_uuid,
              descrs,
              &descr_count) == ESP_OK && descr_count > 0)
            {
              esp_ble_gattc_write_char_descr(gattc_if,
                s_conn_id,
                descrs[0].handle,
                sizeof(cfg),
                (uint8_t*)&cfg,
                ESP_GATT_WRITE_TYPE_RSP,
                ESP_GATT_AUTH_REQ_NONE);
            }
            free(descrs);
          }
        }
      }
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (s_rx_callback && param->notify.value && param->notify.value_len > 0)
      {
        s_rx_callback(param->notify.value, (size_t)param->notify.value_len, s_rx_callback_user_ctx);
      }
      break;
    case ESP_GATTC_READ_CHAR_EVT:
      if (param->read.status == ESP_GATT_OK && s_rx_callback && param->read.value && param->read.value_len > 0)
      {
        s_rx_callback(param->read.value, (size_t)param->read.value_len, s_rx_callback_user_ctx);
      }
      break;
    case ESP_GATTC_DISCONNECT_EVT:
      s_racebox_connected = false;
      s_racebox_connecting = false;
      s_conn_id = 0xFFFF;
      s_service_start_handle = 0;
      s_service_end_handle = 0;
      s_notify_char_count = 0;
      memset(s_notify_char_handles, 0, sizeof(s_notify_char_handles));
      ESP_LOGW(TAG, "RaceBox disconnected, reason=0x%x", param->disconnect.reason);
      racebox_update_status(false, 0, s_device_name, "RaceBox disconnected");
      break;
    default:
      break;
  }
}

static void racebox_start_scan(void)
{
  esp_err_t ret;

  if (s_racebox_connected || s_racebox_connecting || s_gattc_if == ESP_GATT_IF_NONE)
  {
    return;
  }

  if (s_scan_active)
  {
    return;
  }

  ESP_LOGI(TAG,
    "Starting BLE scan interval=0x%x window=0x%x duration=%d targetA=%s targetB=%s",
    CONFIG_RALLYBOX_RACEBOX_SCAN_INTERVAL,
    CONFIG_RALLYBOX_RACEBOX_SCAN_WINDOW,
    CONFIG_RALLYBOX_RACEBOX_SCAN_DURATION_SECONDS,
    TARGET_NAME_A,
    TARGET_NAME_B);
  ret = esp_ble_gap_start_scanning(CONFIG_RALLYBOX_RACEBOX_SCAN_DURATION_SECONDS);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ble_gap_start_scanning() failed: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, s_device_name, "Failed to start BLE scan");
  }
}

esp_err_t racebox_request_scan(void)
{
  if (!s_racebox_initialized || s_gattc_if == ESP_GATT_IF_NONE)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (s_racebox_connected || s_racebox_connecting)
  {
    return ESP_ERR_INVALID_STATE;
  }

  racebox_clear_visible_devices();

  if (s_scan_active)
  {
    racebox_update_status(false, 0, s_device_name, "Scanning for BLE devices...");
    return ESP_OK;
  }

  racebox_start_scan();
  racebox_update_status(false, 0, s_device_name, "Scanning for BLE devices...");
  return ESP_OK;
}

esp_err_t racebox_disconnect(void)
{
  if (!s_racebox_initialized || s_gattc_if == ESP_GATT_IF_NONE)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_racebox_connected || s_conn_id == 0xFFFF)
  {
    return ESP_ERR_INVALID_STATE;
  }

  racebox_update_status(false, s_last_rssi, s_device_name, "Disconnecting from RaceBox...");
  return esp_ble_gattc_close(s_gattc_if, s_conn_id);
}

void racebox_set_rx_callback(racebox_rx_callback_t cb, void* user_ctx)
{
  s_rx_callback = cb;
  s_rx_callback_user_ctx = user_ctx;
}

size_t racebox_get_visible_devices(racebox_visible_device_t* out_devices, size_t max_devices)
{
  size_t i;
  size_t copy_count;

  if (!out_devices || max_devices == 0)
  {
    return 0;
  }

  if (!racebox_ensure_visible_mutex())
  {
    return 0;
  }

  if (xSemaphoreTake(s_visible_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
  {
    return 0;
  }

  copy_count = s_visible_count < max_devices ? s_visible_count : max_devices;
  for (i = 0; i < copy_count; ++i)
  {
    snprintf(out_devices[i].name, sizeof(out_devices[i].name), "%s",
      s_visible_devices[i].name[0] != '\0' ? s_visible_devices[i].name : "Unknown");
    racebox_format_address(s_visible_devices[i].bda, out_devices[i].address, sizeof(out_devices[i].address));
    out_devices[i].rssi = s_visible_devices[i].rssi;
  }

  xSemaphoreGive(s_visible_mutex);
  return copy_count;
}

esp_err_t racebox_connect_visible_device(size_t index)
{
  esp_err_t stop_ret;

  if (!s_racebox_initialized || s_gattc_if == ESP_GATT_IF_NONE)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (s_racebox_connected || s_racebox_connecting)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (!racebox_ensure_visible_mutex())
  {
    return ESP_ERR_NO_MEM;
  }

  if (xSemaphoreTake(s_visible_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
  {
    return ESP_ERR_TIMEOUT;
  }

  if (index >= s_visible_count || !s_visible_devices[index].in_use)
  {
    xSemaphoreGive(s_visible_mutex);
    return ESP_ERR_INVALID_ARG;
  }

  memcpy(s_peer_bda, s_visible_devices[index].bda, sizeof(s_peer_bda));
  s_peer_addr_type = s_visible_devices[index].addr_type;
  s_last_rssi = s_visible_devices[index].rssi;
  snprintf(s_device_name, sizeof(s_device_name), "%s",
    s_visible_devices[index].name[0] != '\0' ? s_visible_devices[index].name : "Unknown");
  xSemaphoreGive(s_visible_mutex);

  s_racebox_connecting = true;
  racebox_update_status(false, s_last_rssi, s_device_name, "Connecting to selected BLE device");

  if (s_scan_active)
  {
    stop_ret = esp_ble_gap_stop_scanning();
    if (stop_ret != ESP_OK)
    {
      s_racebox_connecting = false;
      return stop_ret;
    }
    return ESP_OK;
  }

  return racebox_open_pending_connection();
}

esp_err_t racebox_init(void)
{
  esp_err_t ret;

  if (s_racebox_initialized)
  {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing RaceBox BLE client");

  ret = esp_hosted_connect_to_slave();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Hosted transport unavailable for RaceBox BLE: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, "", "Hosted transport unavailable for RaceBox BLE");
    return ret;
  }

  ret = esp_hosted_bt_controller_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to init hosted BT controller: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, "", "Failed to init hosted BT controller");
    return ret;
  }

  ret = esp_hosted_bt_controller_enable();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to enable hosted BT controller: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, "", "Failed to enable hosted BT controller");
    return ret;
  }

  hosted_hci_bluedroid_open();

  esp_bluedroid_hci_driver_operations_t operations = {
      .send = hosted_hci_bluedroid_send,
      .check_send_available = hosted_hci_bluedroid_check_send_available,
      .register_host_callback = hosted_hci_bluedroid_register_host_callback,
  };
  esp_bluedroid_attach_hci_driver(&operations);

  ret = esp_bluedroid_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to init bluedroid host: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, "", "Failed to init bluedroid host");
    return ret;
  }

  ret = esp_bluedroid_enable();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to enable bluedroid host: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, "", "Failed to enable bluedroid host");
    return ret;
  }

  ret = esp_ble_gap_register_callback(racebox_gap_cb);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to register RaceBox GAP callback: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, "", "Failed to register RaceBox GAP callback");
    return ret;
  }

  ret = esp_ble_gattc_register_callback(racebox_gattc_cb);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to register RaceBox GATTC callback: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, "", "Failed to register RaceBox GATTC callback");
    return ret;
  }

  ret = esp_ble_gattc_app_register(0x42);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to register RaceBox GATTC app: %s", esp_err_to_name(ret));
    racebox_update_status(false, 0, "", "Failed to register RaceBox GATTC app");
    return ret;
  }

  esp_ble_gatt_set_local_mtu(185);
  s_racebox_initialized = true;
  ESP_LOGI(TAG, "RaceBox BLE initialized successfully");
  racebox_update_status(false, 0, "", "RaceBox BLE initialized");
  return ESP_OK;
}