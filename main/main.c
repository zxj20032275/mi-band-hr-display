// main/main.c —— 小米手环心率显示应用:BSP 初始化 + 按键分发 + 心率界面启动。
//
// 按键语义(本应用):
//   上/下 短按   屏幕亮度 +/-10%(NVS 记忆)
//   确定  短按   开始/结束接收手环心率
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "hr_app.h"
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "main";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

static void process_input(const input_event_t *input)
{
    // LVGL 非线程安全:输入任务访问界面前加锁。
    if (!bsp_lvgl_lock(500)) return;
    hr_app_key(input->btn, input->event);
    bsp_lvgl_unlock();
}

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            process_input(&input);
        }
    }
}

static esp_err_t input_dispatch_init(void)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "hr_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// 按键回调运行于共享 esp_timer 任务:只入队并立即返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "小米手环心率显示应用启动");

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }

    // 音频/电量为非关键项,失败不阻塞界面。
    bsp_audio_init();
    bsp_battery_init();

    esp_err_t input_err = input_dispatch_init();
    if (input_err == ESP_OK) {
        esp_err_t button_err = bsp_button_init(on_key, NULL);
        if (button_err != ESP_OK) {
            ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(button_err));
        }
    } else {
        ESP_LOGE(TAG, "输入任务创建失败: %s", esp_err_to_name(input_err));
    }

    if (bsp_lvgl_lock(1000)) {
        hr_app_init();
        bsp_lvgl_unlock();
        s_input_ready = true;
    }
}
