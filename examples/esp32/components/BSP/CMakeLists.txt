set(src_dirs
            mpu6050
            UART
            RGB
            KEY)

set(include_dirs
            mpu6050/include
            UART
            RGB
            KEY)

set(requires
            led_strip
            esp_timer
            driver
            esp_event)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
