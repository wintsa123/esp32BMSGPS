# The following boilerplate must stay in this order for ESP-IDF CMake projects.
cmake_minimum_required(VERSION 3.16)

set(ESP_BMS_PROFILE_FILE "" CACHE FILEPATH "Generated firmware profile")
if(ESP_BMS_PROFILE_FILE)
    if(NOT EXISTS "${ESP_BMS_PROFILE_FILE}")
        message(FATAL_ERROR "ESP_BMS_PROFILE_FILE does not exist: ${ESP_BMS_PROFILE_FILE}")
    endif()
    include("${ESP_BMS_PROFILE_FILE}")
endif()

# Build the application component and the dependencies it declares instead of
# scanning every ESP-IDF component into the project graph.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_bms_gps_idf)
