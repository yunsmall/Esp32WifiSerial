idf_component_register(SRCS "src/main.cpp"
        "src/config_serial.cpp"
        "src/transparent_serial.cpp"
        "src/wifi_serial_manager.cpp"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES sock_utils asio spdlog nvs_flash esp_wifi pthread usbipdcpp driver)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO
        NDEBUG
)
