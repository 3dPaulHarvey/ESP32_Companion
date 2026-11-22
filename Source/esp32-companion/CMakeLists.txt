cmake_minimum_required(VERSION 3.16)

add_compile_options(-Wno-missing-field-initializers)

# Direct idf.py builds use the same standalone configuration as tools/idf.ps1.
set(IDF_TARGET "esp32s3" CACHE STRING "Fixed companion MCU")
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.hardware;config/diagnostics.defaults;config/session.defaults;config/gemini.defaults;config/voice.defaults;config/microphone.defaults;config/question.defaults;config/wake.defaults;config/followup.defaults;config/standalone.defaults")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "0.3.0")
project(esp32_companion)
