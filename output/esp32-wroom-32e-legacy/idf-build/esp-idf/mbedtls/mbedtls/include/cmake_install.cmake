# Install script for directory: /vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include

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

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/mbedtls" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/build_info.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/debug.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/error.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/mbedtls_config.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/net_sockets.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/oid.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/pkcs7.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/ssl.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/ssl_cache.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/ssl_ciphersuites.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/ssl_cookie.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/ssl_ticket.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/timing.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/version.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/x509.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/x509_crl.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/x509_crt.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/x509_csr.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/mbedtls/private" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/private/config_adjust_ssl.h"
    "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/mbedtls/mbedtls/include/mbedtls/private/config_adjust_x509.h"
    )
endif()

