set(src_dirs
    SIM
    DOOM
    FLIP
    GRAVITY
)

set(include_dirs
    SIM
    DOOM
    FLIP
    GRAVITY
)

set(requires
    BSP
    driver
    esp_timer
    esp-dsp
)

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires}
)

component_compile_options(-O3 -ffast-math -Wno-error=format -Wno-format)
