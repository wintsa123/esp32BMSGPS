# Install script for directory: /vol1/1000/项目/esp-idf/esp-idf-v6.0.2

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/wintsa/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32-elf-objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/xtensa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_stdio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_dma/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_gpspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_clock/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_mspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_blockdev/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_security/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_partition/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_app_format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/efuse/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_timg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_timer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_security/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_uart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_pm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_mm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_dma/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/mbedtls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bootloader_format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/bootloader/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esptool_py/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_wdt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_i2s/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_ana_conv/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_rtc_timer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/bootloader_support/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/spi_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_usb_cdc_rom_console/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_system/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_common/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_rom/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/hal/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/log/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/heap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/soc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_usb/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_pmu/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_touch_sens/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hw_support/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/freertos/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_libc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/pthread/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/cxx/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/partition_table/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/app_update/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/http_parser/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_event/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_ringbuf/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_psram/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_uart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_usb_serial_jtag/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/vfs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/lwip/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_http_server/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bms_ota/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/nvs_sec_provider/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/nvs_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_phy/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_netif_stack/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_netif/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/wpa_supplicant/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_coex/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_wifi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_spi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_gdbstub/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/bt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_i2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_twai/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/driver/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_i2s/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_adc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_i2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_ledc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_ledc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_parlio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_bitscrambler/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_driver_parlio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_hal_lcd/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_lcd/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_lcd_touch/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/lvgl__lvgl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__cmake_utilities/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__button/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_new_jpeg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__zlib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__libpng/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_lv_decoder/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_mmap_assets/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_lv_fs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__freetype/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__knob/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_lvgl_adapter/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/atanisoft__esp_lcd_ili9488/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/atanisoft__esp_lcd_touch_xpt2046/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_lcd_st7796/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_lcd_touch_ft5x06/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/espressif__esp_lcd_touch_gt1151/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bms_lvgl_contract/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bms_lvgl_bridge/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bms_lvgl_ui/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_fardriver_protocol/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bms_idf_runtime/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bms_network/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bms_controller_ble/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/esp_bms_bms_ble/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/vol1/1000/项目/esp32BmsGps/output/esp32-wroom-32e-legacy/idf-build/esp-idf/main/cmake_install.cmake")
endif()

