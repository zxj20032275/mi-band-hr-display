// main/hr_app.c —— 心率显示应用:全新 UI、状态机、NVS 亮度、BLE 事件队列。
#include "hr_app.h"

#include "ble_hr.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "hr_clock.h"
#include "hr_zones.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>

static const char *TAG = "hr_app";

#define BRIGHT_DEFAULT   80
#define BRIGHT_MIN       10
#define BRIGHT_MAX       100
#define BRIGHT_STEP      10
#define HR_STALE_MS      5000
#define UI_TIMER_MS      100
#define BLE_QUEUE_DEPTH  16
#define NVS_NAMESPACE    "hrset"
#define NVS_KEY_BRIGHT   "bright"

typedef enum { APP_IDLE = 0, APP_CONNECTING, APP_RUNNING } app_state_t;

// BLE 回调 -> LVGL 任务的消息(BLE 回调不可访问 LVGL)。
typedef struct {
    ble_hr_evt_t evt;
    uint16_t hr;
} ble_msg_t;

static lv_obj_t *s_bg;
static lv_obj_t *s_time_lbl;
static lv_obj_t *s_bat_lbl;
static lv_obj_t *s_hr_lbl;
static lv_obj_t *s_bpm_lbl;
static lv_obj_t *s_status_lbl;
static lv_timer_t *s_timer;
static QueueHandle_t s_queue;

static app_state_t s_state;
static uint8_t s_bright;
static uint16_t s_hr;
static bool s_hr_valid;
static TickType_t s_start_tick;
static TickType_t s_last_hr_tick;
static uint32_t s_frozen_sec;

LV_FONT_DECLARE(hr_cn_16);
LV_FONT_DECLARE(hr_num_104);

// ---------------- NVS 亮度 ----------------

static uint8_t bright_load(void)
{
    uint8_t value = BRIGHT_DEFAULT;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint8_t stored = 0;
        if (nvs_get_u8(h, NVS_KEY_BRIGHT, &stored) == ESP_OK &&
            stored >= BRIGHT_MIN && stored <= BRIGHT_MAX) {
            value = stored;
        }
        nvs_close(h);
    }
    return value;
}

static void bright_save(uint8_t value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_BRIGHT, value);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void bright_apply(uint8_t value)
{
    s_bright = value;
    bsp_display_backlight(value);
    bright_save(value);
}

// ---------------- UI 刷新 ----------------

static void apply_zone_color(void)
{
    hr_zone_t zone = hr_zone_classify((int)s_hr, s_hr_valid);
    lv_obj_set_style_bg_color(s_bg, lv_color_hex(hr_zone_color(zone)), 0);
}

static uint32_t elapsed_sec(void)
{
    if (s_state == APP_IDLE) return s_frozen_sec;
    return (uint32_t)((xTaskGetTickCount() - s_start_tick) * portTICK_PERIOD_MS / 1000);
}

static void set_hr_text(void)
{
    if (s_hr_valid) {
        lv_label_set_text_fmt(s_hr_lbl, "%d", (int)s_hr);
    } else {
        lv_label_set_text(s_hr_lbl, "--");
    }
}

static void set_status_text(ble_hr_evt_t evt)
{
    switch (evt) {
    case BLE_HR_EVT_SCANNING:     lv_label_set_text(s_status_lbl, "搜索中…"); break;
    case BLE_HR_EVT_CONNECTING:   lv_label_set_text(s_status_lbl, "连接中…"); break;
    case BLE_HR_EVT_CONNECTED:    lv_label_set_text(s_status_lbl, "已连接"); break;
    case BLE_HR_EVT_DISCONNECTED: lv_label_set_text(s_status_lbl, "已断开 重连中…"); break;
    case BLE_HR_EVT_FAILED:       lv_label_set_text(s_status_lbl, "蓝牙异常"); break;
    default: break;
    }
}

static void refresh_time(void)
{
    char buf[16];
    if (s_state == APP_IDLE) {
        lv_obj_set_style_text_font(s_time_lbl, &hr_cn_16, 0);
        lv_label_set_text(s_time_lbl, "待机");
        return;
    }
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_24, 0);
    hr_clock_format(elapsed_sec(), buf, sizeof(buf));
    lv_label_set_text(s_time_lbl, buf);
}

static void refresh_battery(void)
{
    int soc = bsp_battery_soc();
    if (soc >= 0 && soc <= 100) {
        lv_label_set_text_fmt(s_bat_lbl, "%d%%", soc);
    } else {
        lv_label_set_text(s_bat_lbl, "");
    }
}

static void ui_tick(lv_timer_t *t)
{
    (void)t;

    ble_msg_t msg;
    while (s_queue && xQueueReceive(s_queue, &msg, 0) == pdTRUE) {
        switch (msg.evt) {
        case BLE_HR_EVT_HR:
            s_hr = msg.hr;
            s_hr_valid = true;
            s_last_hr_tick = xTaskGetTickCount();
            if (s_state != APP_RUNNING) s_state = APP_RUNNING;
            set_hr_text();
            apply_zone_color();
            break;
        case BLE_HR_EVT_CONNECTED:
            s_state = APP_RUNNING;
            set_status_text(msg.evt);
            break;
        case BLE_HR_EVT_DISCONNECTED:
            s_hr_valid = false;
            s_state = APP_CONNECTING;
            set_hr_text();
            apply_zone_color();
            set_status_text(msg.evt);
            break;
        default:
            set_status_text(msg.evt);
            break;
        }
    }

    // 心率过期判定
    if (s_state != APP_IDLE && s_hr_valid &&
        (xTaskGetTickCount() - s_last_hr_tick) * portTICK_PERIOD_MS > HR_STALE_MS) {
        s_hr_valid = false;
        set_hr_text();
        apply_zone_color();
    }

    refresh_time();
    refresh_battery();
}

// ---------------- BLE 回调(NimBLE 任务,只入队) ----------------

static void on_ble(ble_hr_evt_t evt, uint16_t hr, void *arg)
{
    (void)arg;
    if (!s_queue) return;
    const ble_msg_t msg = { .evt = evt, .hr = hr };
    (void)xQueueSend(s_queue, &msg, 0);
}

// ---------------- 会话控制 ----------------

static void session_start(void)
{
    s_hr = 0;
    s_hr_valid = false;
    s_frozen_sec = 0;
    s_start_tick = xTaskGetTickCount();
    s_state = APP_CONNECTING;
    set_hr_text();
    apply_zone_color();
    set_status_text(BLE_HR_EVT_SCANNING);
    esp_err_t err = ble_hr_start(on_ble, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ble_hr_start: %s", esp_err_to_name(err));
        lv_label_set_text(s_status_lbl, "启动失败");
        s_state = APP_IDLE;
    }
}

static void session_stop(void)
{
    s_frozen_sec = elapsed_sec();
    esp_err_t err = ble_hr_stop();
    if (err != ESP_OK) ESP_LOGW(TAG, "ble_hr_stop: %s", esp_err_to_name(err));
    s_state = APP_IDLE;
    s_hr_valid = false;
    set_hr_text();
    apply_zone_color();
}

// ---------------- 界面构建 ----------------

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_text_align_t align)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl, align, 0);
    return lbl;
}

static void ui_build(void)
{
    s_bg = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_bg, 0, 0);
    lv_obj_set_style_border_width(s_bg, 0, 0);
    lv_obj_set_style_pad_all(s_bg, 0, 0);
    lv_obj_set_size(s_bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_bg, lv_color_hex(hr_zone_color(HR_ZONE_IDLE)), 0);
    lv_obj_set_style_bg_opa(s_bg, LV_OPA_COVER, 0);

    // 左上电量
    s_bat_lbl = make_label(s_bg, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(s_bat_lbl, LV_ALIGN_TOP_LEFT, 8, 8);

    // 顶部居中时间
    s_time_lbl = make_label(s_bg, &hr_cn_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_MID, 0, 8);

    // 中部大心率
    s_hr_lbl = make_label(s_bg, &hr_num_104, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_hr_lbl, LV_ALIGN_CENTER, 0, -28);
    lv_label_set_text(s_hr_lbl, "--");

    s_bpm_lbl = make_label(s_bg, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_bpm_lbl, LV_ALIGN_CENTER, 0, 52);
    lv_label_set_text(s_bpm_lbl, "BPM");

    s_status_lbl = make_label(s_bg, &hr_cn_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, 82);
    lv_label_set_text(s_status_lbl, "");

    // 底部按键提示
    lv_obj_t *hints = make_label(s_bg, &hr_cn_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(hints, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_text(hints, "上键 亮度+\n下键 亮度-\nOK 开始/停止");
}

// ---------------- 对外接口 ----------------

void hr_app_init(void)
{
    s_state = APP_IDLE;
    s_hr = 0;
    s_hr_valid = false;
    s_frozen_sec = 0;

    // 确保 NVS 可用(亮度存取)。
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK && nvs_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "nvs_flash_init: %s", esp_err_to_name(nvs_err));
    }

    s_bright = bright_load();
    bsp_display_backlight(s_bright);

    s_queue = xQueueCreate(BLE_QUEUE_DEPTH, sizeof(ble_msg_t));

    ui_build();
    s_timer = lv_timer_create(ui_tick, UI_TIMER_MS, NULL);
}

void hr_app_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev == BSP_BTN_CLICK && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        int16_t next = (int16_t)s_bright +
                       (btn == BSP_BTN_UP ? BRIGHT_STEP : -BRIGHT_STEP);
        if (next < BRIGHT_MIN) next = BRIGHT_MIN;
        if (next > BRIGHT_MAX) next = BRIGHT_MAX;
        bright_apply((uint8_t)next);
        return;
    }

    if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        if (s_state == APP_IDLE) {
            session_start();
        } else {
            session_stop();
        }
    }
}
