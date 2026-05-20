# SeeedMote v2 — gateway main component build manifest (ESP-IDF).
#
# PlatformIO synthesises the rest of the IDF project layout around this file.
# To add a new source, append it to SRCS.

idf_component_register(
    SRCS        "main.c"
                "bthome.c"
                "adv_ring.c"
                "ble_observer.c"
                "mqtt_gateway.c"
                "wifi_mgmt.c"
    INCLUDE_DIRS "."
    REQUIRES    driver esp_common esp_system freertos log bt nvs_flash
                esp_wifi esp_event esp_netif mqtt
)
