idf_component_register(
    SRCS "app_main.cpp" "save_store.cpp" "nvs_backend.cpp" "usage_nvs.cpp" "practice_nvs.cpp" "board_hal.cpp"
         "handheld_runtime.cpp" "handheld_ui.cpp" "handheld_walking.cpp" "handheld_nearby.cpp" "handheld_idle.cpp" "idle_settings.cpp" "nearby_radio.cpp" "network_adapter.cpp" "device_setup.cpp" "motion_adapter.cpp"
         "display_touch.cpp" "third_party/spd2010/esp_lcd_spd2010.c" "device_audio.cpp" "device_imu.cpp" "device_art.cpp" "sd_inspect.cpp"
         "../runtime/device_ui.cpp" "../runtime/setup_ui.cpp" "../runtime/audio_cues.cpp" "../runtime/imu_filter.cpp" "../runtime/pedometer.cpp" "../runtime/usage_store.cpp" "../runtime/idle.cpp" "../runtime/idle_settings.cpp" "../runtime/battle_presentation.cpp" "../runtime/nearby_protocol.cpp" "../../core/nearby_match.cpp" "../runtime/background_decode.cpp"
         "asset_partition.cpp" "sd_asset_storage.cpp" "device_assets_client.cpp"
         "../runtime/power.cpp" "../runtime/network.cpp" "../runtime/motion.cpp" "../runtime/step_delivery.cpp" "../runtime/recovery_choice.cpp" "../runtime/starter.cpp" "../runtime/battle_mode_choice.cpp" "../runtime/practice_session.cpp" "../runtime/evolution_choice.cpp" "../runtime/release_confirmation.cpp" "../runtime/local_form_art.cpp"
         "../runtime/file_asset_storage.cpp" "../runtime/asset_cache.cpp" "../runtime/sprite.cpp" "../runtime/prefetch.cpp" "../runtime/usb_sd_transfer.cpp"
         "../../core/game.cpp" "../../core/combat.cpp" "../../core/encounters.cpp" "../../core/forms.cpp" "../../core/legacy_combat_v3.cpp" "../../core/legacy_forms_v5.cpp" "../../core/legacy_combat_v5.cpp" "../../core/legacy_forms_v6.cpp" "../../core/legacy_combat_v6.cpp" "../../core/legacy_forms_v7.cpp" "../../core/legacy_combat_v7.cpp" "../../core/legacy_forms_v8.cpp" "../../core/legacy_combat_v8.cpp" "../../core/legacy_forms_v9.cpp" "../../core/legacy_combat_v9.cpp" "../../core/battle_trace.cpp" "../../core/practice_battle.cpp"
    INCLUDE_DIRS "." "../runtime" "../../core"
    REQUIRES esp_wifi nvs_flash esp_system esp_app_format freertos esp_event esp_netif
             esp_http_client esp_timer esp_hw_support esp_partition
             esp_driver_gpio esp_driver_i2c esp_driver_sdmmc esp_driver_usb_serial_jtag esp_driver_spi esp_driver_ledc esp_driver_i2s esp_lcd esp_psram sdmmc fatfs mbedtls json
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
