idf_component_register(SRCS "esp32-net-speedtest.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_netif esp_event nvs_flash lwip esp_timer)
