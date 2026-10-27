idf_component_register(
    SRCS "main.c"
         "hr_app.c"
         "hr_zones.c"
         "hr_clock.c"
         "hr_parse.c"
         "ble_hr.c"
         "demo_radio.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_timer nvs_flash
)

# 应用自定义字体子集(Noto Sans CJK SC,OFL;由 assets/fonts/generate 流程生成)。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/hr_cn_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/hr_num_104.c"
)
