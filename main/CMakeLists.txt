# Path to your EEZ UI component (already in components/ui)
set(UI_DEMO_DIR ${CMAKE_CURRENT_LIST_DIR}/../components/ui)

set(MAIN_SRCS
    main.c
    system_monitor.c
    display_brightness.c
    sd_card.c
    GPS_points.c
    ui_logic.c
)

set(MAIN_REQUIRES
    ui
    lvgl__lvgl
    esp32_p4_wifi6_touch_lcd_x
    bsp_extra
    nvs_flash
    driver
    freertos
    fatfs
    esp_wifi
    esp_event
    esp_netif
    esp_http_client
    esp_https_ota
    app_update
    mbedtls
)

if(CONFIG_RALLYBOX_RACEBOX_ENABLED)
    list(APPEND MAIN_SRCS racebox.c)
    list(APPEND MAIN_REQUIRES bt esp_hosted)
    list(APPEND MAIN_PRIV_INCLUDES "${IDF_PATH}/components/bt/host/bluedroid/api/include/api")
endif()

if(CONFIG_RALLYBOX_GNSS_ENABLED)
    list(APPEND MAIN_SRCS gnss.c)
endif()

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."            # main folder
    PRIV_INCLUDE_DIRS "../components/ui" ${MAIN_PRIV_INCLUDES}
    REQUIRES ${MAIN_REQUIRES}
)

# LVGL compile options
idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)