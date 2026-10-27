// main/ble_hr.h —— 小米手环标准心率服务(HRS 0x180D)中心连接模块。
//
// 流程:扫描(广播含 0x180D 的设备) -> 连接 -> 交换 MTU -> 发现 HRS ->
//       发现 Heart Rate Measurement(0x2A37) -> 订阅 NOTIFY -> 上报心率。
// 活动期间断线/连接失败自动重新扫描;stop 由调用方触发。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BLE_HR_EVT_SCANNING = 0,  // 正在搜索手环
    BLE_HR_EVT_CONNECTING,    // 已发现目标,连接中
    BLE_HR_EVT_CONNECTED,     // 已连接并完成订阅
    BLE_HR_EVT_HR,            // 收到有效心率(hr 字段)
    BLE_HR_EVT_DISCONNECTED,  // 断开,将自动重连
    BLE_HR_EVT_FAILED,        // 不可恢复失败
} ble_hr_evt_t;

// 事件回调。在 NimBLE host 任务上下文执行:只做入队/有界操作,勿阻塞或访问 LVGL。
typedef void (*ble_hr_cb_t)(ble_hr_evt_t evt, uint16_t hr, void *arg);

// 启动接收(初始化 NimBLE 并开始扫描)。成功调用可重复,已启动返回 INVALID_STATE。
esp_err_t ble_hr_start(ble_hr_cb_t cb, void *arg);

// 停止接收(断开连接、停止并反初始化 NimBLE)。
esp_err_t ble_hr_stop(void);
