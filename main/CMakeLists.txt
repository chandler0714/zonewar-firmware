idf_component_register(
    SRCS
        "main.c"
        "zw_nvs.c"
        "zw_wifi.c"
        "zw_ble.c"
        // "zw_provision.c"
        "zw_api.c"
        "zw_ui.c"
        "ui_team_select.c"
        "ui_dashboard.c"
        "ui_capture.c"
        "ui_add_marker.c"
        "ui_marker_list.c"
        "ui_wifi_config.c"
    INCLUDE_DIRS "."
    REQUIRES
        bsp
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        // esp_http_server
        esp_timer
        bt
        lvgl
        json
)
