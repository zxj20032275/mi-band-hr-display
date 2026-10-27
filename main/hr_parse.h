// main/hr_parse.h —— 蓝牙数据纯解析(与 NimBLE/LVGL 解耦,可主机测试)。
//
// 包含两部分:
//   1. 广播包 TLV 解析:判断设备是否在 16-bit Service UUID 列表中声明心率服务 0x180D。
//   2. Heart Rate Measurement(0x2A37)通知负载解析:按 HRS 规范取心率值。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HR_SERVICE_UUID        0x180D   // Heart Rate Service
#define HR_MEASURE_CHAR_UUID   0x2A37   // Heart Rate Measurement

// BLE 广播 AD Type。
#define HR_AD_TYPE_UUID16_INCOMPLETE  0x02
#define HR_AD_TYPE_UUID16_COMPLETE    0x03
#define HR_AD_TYPE_UUID128_INCOMPLETE 0x06
#define HR_AD_TYPE_UUID128_COMPLETE   0x07
#define HR_AD_TYPE_NAME_SHORT         0x08
#define HR_AD_TYPE_NAME_COMPLETE      0x09
#define HR_AD_TYPE_SERVICE_DATA16     0x16
#define HR_AD_TYPE_MFG_SPECIFIC       0xFF

// 遍历原始广播字节 data(长度 len),若任一 16-bit UUID 列表(完整或不完整)包含
// uuid,返回 true。data 为 AD 结构序列:[len][type][payload]...
bool hr_adv_has_uuid16(const uint8_t *data, size_t len, uint16_t uuid);

// Heart Rate Measurement 解析结果。
typedef struct {
    bool valid;       // 负载完整且心率在可解析范围
    uint16_t hr;      // 心率(bpm)
    bool is_uint16;   // flags bit0=1,心率以 16 位传输
} hr_meas_t;

// 解析 Heart Rate Measurement 特征通知/指示负载(不含 ATT 句柄前缀)。
//   byte0 flags;bit0:0=uint8 心率,1=uint16(小端);其余字段忽略。
hr_meas_t hr_parse_measurement(const uint8_t *data, size_t len);
