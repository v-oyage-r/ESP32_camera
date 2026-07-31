set(src_dirs
            .)

set(include_dirs
            .)

set(requires
            esp_wifi
            esp_event
            esp_netif
            esp_http_server
            fatfs
            sdmmc
            esp32-camera
            esp-dl
            BSP
            )

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
