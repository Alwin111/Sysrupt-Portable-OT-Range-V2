file(GLOB_RECURSE SOURCE_FILES source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "source"
    REQUIRES TactilityKernel lvgl-module lvgl-window-manager-module app-module esp_timer heap lwip esp_wifi esp_netif esp_http_server nvs_flash esp_driver_ledc
)
