# Fixed XIAO ESP32-S3 companion: OLED face, simplex I2S and local AFE wake.
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "ESP32 Companion supports only the verified ESP32-S3 hardware")
endif()

set(SOURCES
    "audio/audio_codec.cc"
    "audio/audio_service.cc"
    "audio/codecs/no_audio_codec.cc"
    "audio/wake_words/afe_wake_word.cc"
    "boards/seeedstudio-s3-wifi/seeedstudio_s3_wifi_board.cc"
    "display/oled_display.cc"
    "assets.cc"
    "hardware_diagnostics.cc"
    "main.cc"
)

if(CONFIG_COMPANION_SESSION_PROBE)
    list(APPEND SOURCES "companion/session_probe.cc" "../tests/companion_session_policy_test.cpp")
endif()
if(CONFIG_COMPANION_GEMINI_SETUP_PROBE)
    list(APPEND SOURCES "companion/gemini_setup.cc" "../tests/companion_gemini_policy_test.cpp")
endif()
if(CONFIG_COMPANION_GEMINI_VOICE_PROBE)
    list(APPEND SOURCES "companion/gemini_voice.cc" "../tests/companion_voice_policy_test.cpp")
endif()
if(CONFIG_COMPANION_MICROPHONE_PREP)
    list(APPEND SOURCES "../tests/companion_microphone_policy_test.cpp")
endif()
if(CONFIG_COMPANION_GEMINI_QUESTION_PROBE)
    list(APPEND SOURCES "../tests/companion_question_policy_test.cpp")
endif()
if(CONFIG_COMPANION_CLOUD_WAKE)
    list(APPEND SOURCES "../tests/companion_wake_policy_test.cpp")
endif()
if(CONFIG_COMPANION_FOLLOWUP)
    list(APPEND SOURCES "companion/gemini_conversation.cc" "companion/question_stream.cc"
        "../tests/companion_followup_policy_test.cpp" "../tests/companion_transport_policy_test.cpp"
        "../tests/companion_endpoint_policy_test.cpp" "../tests/companion_live_session_policy_test.cpp"
        "../tests/companion_audio_stream_policy_test.cpp")
endif()
if(CONFIG_COMPANION_STANDALONE)
    list(APPEND SOURCES "../tests/companion_standalone_policy_test.cpp")
endif()
if(COMPANION_QUESTION_FIXTURE)
    list(APPEND SOURCES "companion/question_fixture.cc")
endif()

# A stale menuconfig must not silently select an incompatible flash layout.
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND (NOT CONFIG_ESPTOOLPY_FLASHSIZE_8MB OR
   NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions/v2/8m.csv"))
    message(FATAL_ERROR "Use sdkconfig.hardware: this board requires 8 MB flash and partitions/v2/8m.csv")
endif()

idf_component_register(SRCS ${SOURCES}
    EMBED_FILES "assets/common/popup.ogg"
    INCLUDE_DIRS "." "audio" "display" "boards/common"
    PRIV_REQUIRES
        esp_psram esp_netif esp_wifi esp_event esp_timer esp_lcd
        esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_usb_serial_jtag
        esp_app_format spi_flash nvs_flash vfs json esp_http_client mbedtls tcp_transport
)

if(COMPANION_QUESTION_FIXTURE)
    get_filename_component(QUESTION_FIXTURE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../logs/robot-question-public-fixture.pcm" ABSOLUTE)
    if(NOT EXISTS "${QUESTION_FIXTURE_FILE}")
        message(FATAL_ERROR "Prepare the public synthetic question fixture before using FixtureCheck")
    endif()
    file(SIZE "${QUESTION_FIXTURE_FILE}" QUESTION_FIXTURE_SIZE)
    if(NOT QUESTION_FIXTURE_SIZE EQUAL 128000)
        message(FATAL_ERROR "The public question fixture must be exactly 128000 bytes")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${QUESTION_FIXTURE_FILE}" BINARY)
    if(CONFIG_COMPANION_FOLLOWUP)
        get_filename_component(FOLLOWUP_FIXTURE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../logs/robot-followup-public-fixture.pcm" ABSOLUTE)
        if(NOT EXISTS "${FOLLOWUP_FIXTURE_FILE}")
            message(FATAL_ERROR "Prepare the public follow-up fixture before FixtureCheck")
        endif()
        file(SIZE "${FOLLOWUP_FIXTURE_FILE}" FOLLOWUP_FIXTURE_SIZE)
        if(NOT FOLLOWUP_FIXTURE_SIZE EQUAL 128000)
            message(FATAL_ERROR "The public follow-up fixture must be exactly 128000 bytes")
        endif()
        target_add_binary_data(${COMPONENT_LIB} "${FOLLOWUP_FIXTURE_FILE}" BINARY)
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE COMPANION_QUESTION_FIXTURE=1)
endif()

if(CONFIG_COMPANION_GEMINI_SETUP_PROBE)
    # The transport's upgrade-error log can contain the temporary access token.
    idf_component_get_property(COMPANION_TRANSPORT_LIB tcp_transport COMPONENT_LIB)
    target_compile_definitions(${COMPANION_TRANSPORT_LIB} PRIVATE LOG_LOCAL_LEVEL=0)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE COMPANION_WS_LOGS_DISABLED=1)
endif()

# Keep the flash format and partition map compatible with saved recovery images.
idf_component_get_property(ESP_SR_COMPONENT_PATH espressif__esp-sr COMPONENT_DIR)
set(WAKE_MODEL_DIR "${ESP_SR_COMPONENT_PATH}/model/wakenet_model/wn9_hiesp")
file(GLOB WAKE_MODEL_FILES "${WAKE_MODEL_DIR}/*")
set(GENERATED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
add_custom_command(
    OUTPUT "${GENERATED_ASSETS_BIN}"
    COMMAND "${PYTHON}" "${PROJECT_DIR}/scripts/build_wake_assets.py"
        --model-dir "${WAKE_MODEL_DIR}" --output "${GENERATED_ASSETS_BIN}"
    DEPENDS ${WAKE_MODEL_FILES} "${PROJECT_DIR}/scripts/build_wake_assets.py"
    COMMENT "Packing the fixed Hi ESP wake model"
    VERBATIM
)
add_custom_target(companion_wake_assets ALL DEPENDS "${GENERATED_ASSETS_BIN}")
esptool_py_flash_to_partition(flash "assets" "${GENERATED_ASSETS_BIN}")
