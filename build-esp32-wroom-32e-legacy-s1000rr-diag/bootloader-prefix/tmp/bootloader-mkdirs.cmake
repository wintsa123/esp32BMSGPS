# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/vol1/1000/项目/esp-idf/esp-idf-v6.0.2/components/bootloader/subproject"
  "/vol1/1000/项目/esp32BmsGps/build-esp32-wroom-32e-legacy-s1000rr-diag/bootloader"
  "/vol1/1000/项目/esp32BmsGps/build-esp32-wroom-32e-legacy-s1000rr-diag/bootloader-prefix"
  "/vol1/1000/项目/esp32BmsGps/build-esp32-wroom-32e-legacy-s1000rr-diag/bootloader-prefix/tmp"
  "/vol1/1000/项目/esp32BmsGps/build-esp32-wroom-32e-legacy-s1000rr-diag/bootloader-prefix/src/bootloader-stamp"
  "/vol1/1000/项目/esp32BmsGps/build-esp32-wroom-32e-legacy-s1000rr-diag/bootloader-prefix/src"
  "/vol1/1000/项目/esp32BmsGps/build-esp32-wroom-32e-legacy-s1000rr-diag/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/vol1/1000/项目/esp32BmsGps/build-esp32-wroom-32e-legacy-s1000rr-diag/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/vol1/1000/项目/esp32BmsGps/build-esp32-wroom-32e-legacy-s1000rr-diag/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
