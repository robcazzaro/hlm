# ESP32Common/CMakeLists.txt
idf_component_register(
    SRCS
        "hub_app.c"
        "glm_library.c"
        "oled.c"
        # ... other common sources
    INCLUDE_DIRS
        "."
        # any subdirectories with headers
)