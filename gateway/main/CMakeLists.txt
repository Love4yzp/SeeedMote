idf_component_register(
    SRCS "main.c"
         "gw_id.c"
         "nvs_config.c"
         "wifi_mgr.c"
         "web_server.c"
         "mqtt_mgr.c"
         "ble_scanner.c"
         "led.c"
         "cli.c"
         "web_ui_html.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_common esp_system freertos log
             bt nvs_flash esp_wifi esp_event esp_netif
             esp_http_server mqtt json lwip esp_timer
             console vfs
)
