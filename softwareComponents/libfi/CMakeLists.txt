file(GLOB_RECURSE SRC src/*.cpp src/*.c)

# Specify optional dependencies from ESP-IDF
set(COMPONENTS "")

idf_component_register(SRCS ${SRC}
    INCLUDE_DIRS "." "src"
    REQUIRES "driver" "nvs_flash" "esp_http_client")
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PUBLIC -mtext-section-literals -mlongcalls)
