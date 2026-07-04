# Tech Stack

- No committed firmware stack yet: no `platformio.ini`, ESP-IDF `CMakeLists.txt`, Arduino sketch, `sdkconfig`, or source directory exists.
- Trellis scaffolding uses Python scripts under `.trellis/scripts/` for task/session workflow; this is project tooling, not the target firmware runtime.
- Serena detected Python because of Trellis scripts, but the intended product is embedded ESP32 firmware. Do not infer Python as the application language.
- Framework decision is still open until planning resolves Arduino/PlatformIO vs ESP-IDF and the target ESP32 board/display/touch/BMS hardware.