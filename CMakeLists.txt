# The following boilerplate must stay in this order for ESP-IDF CMake projects.
cmake_minimum_required(VERSION 3.16)

# Build the application component and the dependencies it declares instead of
# scanning every ESP-IDF component into the project graph.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_bms_gps_idf)
